/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.search

import android.content.Context
import android.content.Intent
import android.content.res.Resources
import android.speech.RecognizerIntent
import androidx.annotation.VisibleForTesting
import androidx.core.graphics.drawable.toDrawable
import androidx.navigation.NavController
import kotlinx.coroutines.CoroutineExceptionHandler
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Job
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.asCoroutineDispatcher
import kotlinx.coroutines.cancelChildren
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.distinctUntilChanged
import kotlinx.coroutines.flow.distinctUntilChangedBy
import kotlinx.coroutines.flow.map
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import mozilla.components.browser.state.action.AwesomeBarAction.EngagementFinished
import mozilla.components.browser.state.search.SearchEngine
import mozilla.components.browser.state.search.SearchEngine.Type.APPLICATION
import mozilla.components.browser.state.search.SearchEngine.Type.CUSTOM
import mozilla.components.browser.state.state.selectedOrDefaultSearchEngine
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.compose.browser.toolbar.BrowserToolbar
import mozilla.components.compose.browser.toolbar.concept.Action
import mozilla.components.compose.browser.toolbar.concept.Action.ActionButton
import mozilla.components.compose.browser.toolbar.concept.Action.ActionButtonRes
import mozilla.components.compose.browser.toolbar.concept.Action.SearchSelectorAction
import mozilla.components.compose.browser.toolbar.store.BrowserEditToolbarAction
import mozilla.components.compose.browser.toolbar.store.BrowserEditToolbarAction.HintUpdated
import mozilla.components.compose.browser.toolbar.store.BrowserEditToolbarAction.SearchActionsEndUpdated
import mozilla.components.compose.browser.toolbar.store.BrowserEditToolbarAction.SearchActionsStartUpdated
import mozilla.components.compose.browser.toolbar.store.BrowserEditToolbarAction.SearchQueryUpdated
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarAction
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarAction.CommitUrl
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarAction.EnterEditMode
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarAction.ExitEditMode
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarAction.Init
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarInteraction.BrowserToolbarEvent
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarInteraction.BrowserToolbarMenu
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarMenuItem
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarMenuItem.BrowserToolbarMenuButton
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarMenuItem.BrowserToolbarMenuDivider
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarState
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarStore
import mozilla.components.compose.browser.toolbar.ui.BrowserToolbarQuery
import mozilla.components.concept.engine.EngineSession
import mozilla.components.concept.toolbar.AutocompleteProvider
import mozilla.components.concept.toolbar.AutocompleteResult
import mozilla.components.lib.state.Middleware
import mozilla.components.lib.state.State
import mozilla.components.lib.state.Store
import mozilla.components.lib.state.ext.flow
import mozilla.components.support.base.log.logger.Logger
import mozilla.components.support.base.utils.NamedThreadFactory
import mozilla.components.support.ktx.kotlin.isUrl
import org.mozilla.fenix.GleanMetrics.Events
import org.mozilla.fenix.GleanMetrics.Toolbar
import org.mozilla.fenix.NavGraphDirections
import org.mozilla.fenix.R
import org.mozilla.fenix.browser.BrowserFragmentDirections
import org.mozilla.fenix.components.AppStore
import org.mozilla.fenix.components.Components
import org.mozilla.fenix.components.appstate.AppAction
import org.mozilla.fenix.components.appstate.AppAction.QrScannerAction.QrScannerRequested
import org.mozilla.fenix.components.appstate.AppAction.SearchAction.SearchEnded
import org.mozilla.fenix.components.appstate.AppAction.SearchAction.SearchEngineSelected
import org.mozilla.fenix.components.appstate.AppAction.SearchAction.SearchStarted
import org.mozilla.fenix.components.appstate.VoiceSearchAction.VoiceInputRequestCleared
import org.mozilla.fenix.components.appstate.VoiceSearchAction.VoiceInputRequested
import org.mozilla.fenix.components.metrics.MetricsUtils
import org.mozilla.fenix.components.search.BOOKMARKS_SEARCH_ENGINE_ID
import org.mozilla.fenix.components.search.HISTORY_SEARCH_ENGINE_ID
import org.mozilla.fenix.components.search.TABS_SEARCH_ENGINE_ID
import org.mozilla.fenix.ext.toolbarHintRes
import org.mozilla.fenix.search.EditPageEndActionsInteractions.ClearSearchClicked
import org.mozilla.fenix.search.EditPageEndActionsInteractions.QrScannerClicked
import org.mozilla.fenix.search.EditPageEndActionsInteractions.VoiceSearchButtonClicked
import org.mozilla.fenix.search.SearchSelectorEvents.SearchSelectorClicked
import org.mozilla.fenix.search.SearchSelectorEvents.SearchSelectorItemClicked
import org.mozilla.fenix.search.SearchSelectorEvents.SearchSettingsItemClicked
import org.mozilla.fenix.search.ext.searchEngineShortcuts
import org.mozilla.fenix.settings.SupportUtils
import org.mozilla.fenix.telemetry.ACTION_CLEAR_CLICKED
import org.mozilla.fenix.telemetry.ACTION_MICROPHONE_CLICKED
import org.mozilla.fenix.telemetry.ACTION_QR_CLICKED
import org.mozilla.fenix.telemetry.ACTION_SEARCH_ENGINE_SELECTOR_CLICKED
import org.mozilla.fenix.telemetry.SOURCE_ADDRESS_BAR
import org.mozilla.fenix.utils.Settings
import java.util.concurrent.Executors
import kotlin.coroutines.CoroutineContext
import mozilla.components.browser.toolbar.R as toolbarR
import mozilla.components.compose.browser.toolbar.concept.Action.SearchSelectorAction.ContentDescription.StringContentDescription as SearchSelectorDescription
import mozilla.components.compose.browser.toolbar.concept.Action.SearchSelectorAction.Icon.DrawableIcon as SearchSelectorIcon
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarMenuItem.BrowserToolbarMenuButton.ContentDescription.StringContentDescription as MenuItemStringDescription
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarMenuItem.BrowserToolbarMenuButton.ContentDescription.StringResContentDescription as MenuItemDescriptionRes
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarMenuItem.BrowserToolbarMenuButton.Icon.DrawableIcon as MenuItemIcon
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarMenuItem.BrowserToolbarMenuButton.Icon.DrawableResIcon as MenuItemIconRes
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarMenuItem.BrowserToolbarMenuButton.Text.StringResText as MenuItemStringResText
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarMenuItem.BrowserToolbarMenuButton.Text.StringText as MenuItemStringText
import mozilla.components.feature.qr.R as qrR
import mozilla.components.lib.state.Action as MVIAction
import mozilla.components.ui.icons.R as iconsR

@VisibleForTesting
internal sealed class SearchSelectorEvents : BrowserToolbarEvent {
    data object SearchSelectorClicked : SearchSelectorEvents()

    data object SearchSettingsItemClicked : SearchSelectorEvents()

    data class SearchSelectorItemClicked(
        val searchEngine: SearchEngine,
    ) : SearchSelectorEvents()
}

@VisibleForTesting
internal sealed class EditPageEndActionsInteractions : BrowserToolbarEvent {
    data object ClearSearchClicked : EditPageEndActionsInteractions()
    data object QrScannerClicked : EditPageEndActionsInteractions()

    data object VoiceSearchButtonClicked : SearchSelectorEvents()
}

/**
 * [BrowserToolbarStore] middleware handling the configuration of the composable toolbar
 * while in edit mode.
 *
 * @param uiContext [Context] used for various system interactions.
 * @param appStore [AppStore] used for querying and updating application state.
 * @param browserStore [BrowserStore] used for querying and updating browser state.
 * @param components [Components] for accessing other functionalities of the application.
 * @param navController [NavController] to use for navigating to other in-app destinations.
 * @param settings [Settings] for accessing application settings.
 * @param scope [CoroutineScope] used for running long running operations in background.
 * @param autocompleteDispatcher [CoroutineContext] used for querying autocomplete suggestions.
 */
@Suppress("LongParameterList")
class BrowserToolbarSearchMiddleware(
    private val uiContext: Context,
    private val appStore: AppStore,
    private val browserStore: BrowserStore,
    private val components: Components,
    private val navController: NavController,
    private val settings: Settings,
    private val scope: CoroutineScope,
    private val autocompleteDispatcher: CoroutineContext = defaultAutocompleteDispatcher,
) : Middleware<BrowserToolbarState, BrowserToolbarAction> {
    private var syncCurrentSearchEngineJob: Job? = null
    private var syncAvailableSearchEnginesJob: Job? = null
    private var observeQRScannerInputJob: Job? = null
    private var observeVoiceInputJob: Job? = null
    private var updateAutocompleteJob: Job? = null

    @Suppress("CyclomaticComplexMethod", "LongMethod")
    override fun invoke(
        store: Store<BrowserToolbarState, BrowserToolbarAction>,
        next: (BrowserToolbarAction) -> Unit,
        action: BrowserToolbarAction,
    ) {
        if (action !is SearchSelectorEvents) {
            next(action)
        }

        when (action) {
            is Init -> {
                if (store.state.isEditMode()) {
                    syncCurrentSearchEngine(store)
                }
            }

            is EnterEditMode -> {
                refreshConfigurationAfterSearchEngineChange(
                    store = store,
                    searchEngine = this.reconcileSelectedEngine(),
                )
                observeVoiceInputResults(store)
                syncCurrentSearchEngine(store)
                syncAvailableEngines(store)
                updateSearchEndPageActions(store)
            }

            is ExitEditMode -> {
                syncCurrentSearchEngineJob?.cancel()
                syncAvailableSearchEnginesJob?.cancel()

                if (observeQRScannerInputJob?.isActive == true) {
                    appStore.dispatch(AppAction.QrScannerAction.QrScannerDismissed)
                }
                observeQRScannerInputJob?.cancel()
                observeVoiceInputJob?.cancel()
            }

            is SearchSelectorClicked -> {
                Toolbar.buttonTapped.record(
                    Toolbar.ButtonTappedExtra(
                        source = SOURCE_ADDRESS_BAR,
                        item = ACTION_SEARCH_ENGINE_SELECTOR_CLICKED,
                    ),
                )
            }

            is SearchSettingsItemClicked -> {
                store.dispatch(SearchQueryUpdated(BrowserToolbarQuery("")))
                appStore.dispatch(SearchEnded)
                browserStore.dispatch(EngagementFinished(abandoned = true))
                navController.navigate(
                    BrowserFragmentDirections.actionGlobalSearchEngineFragment(),
                )
            }

            is SearchSelectorItemClicked -> {
                appStore.dispatch(SearchEngineSelected(action.searchEngine, true))
                appStore.dispatch(SearchStarted())
                refreshConfigurationAfterSearchEngineChange(store, action.searchEngine)
                updateSearchEndPageActions(store) // to update the visibility of the qr scanner button
            }

            is CommitUrl -> {
                // Do not load URL if application search engine is selected.
                if (reconcileSelectedEngine()?.type == SearchEngine.Type.APPLICATION) {
                    return
                }

                when (action.text) {
                    "about:crashes" -> {
                        // The list of past crashes can be accessed via "settings > about", but desktop and
                        // fennec users may be used to navigating to "about:crashes". So we intercept this here
                        // and open the crash list activity instead.
                        navController.navigate(
                            NavGraphDirections.actionGlobalCrashListFragment(),
                        )
                    }
                    "about:addons" -> {
                        navController.navigate(
                            NavGraphDirections.actionGlobalAddonsManagementFragment(),
                        )
                        browserStore.dispatch(EngagementFinished(abandoned = false))
                    }
                    "about:glean" -> {
                        navController.navigate(
                            NavGraphDirections.actionGlobalGleanDebugToolsFragment(),
                        )
                    }
                    "moz://a" -> openSearchOrUrl(
                        SupportUtils.getMozillaPageUrl(SupportUtils.MozillaPage.MANIFESTO),
                        navController,
                    )
                    else ->
                        if (action.text.isNotBlank()) {
                            openSearchOrUrl(action.text, navController)
                        } else {
                            browserStore.dispatch(EngagementFinished(abandoned = true))
                        }
                }

                appStore.dispatch(SearchEnded)
            }

            is ClearSearchClicked -> {
                Toolbar.buttonTapped.record(
                    Toolbar.ButtonTappedExtra(source = SOURCE_ADDRESS_BAR, item = ACTION_CLEAR_CLICKED),
                )
                store.dispatch(SearchQueryUpdated(BrowserToolbarQuery("")))
            }

            is SearchQueryUpdated -> {
                updateAutocompletions(store, action.query)
                updateSearchEndPageActions(store)
            }

            is QrScannerClicked -> {
                Toolbar.buttonTapped.record(
                    Toolbar.ButtonTappedExtra(source = SOURCE_ADDRESS_BAR, item = ACTION_QR_CLICKED),
                )
                observeQrScannerInput(store)
                appStore.dispatch(QrScannerRequested)
            }

            is VoiceSearchButtonClicked -> {
                Toolbar.buttonTapped.record(
                    Toolbar.ButtonTappedExtra(source = SOURCE_ADDRESS_BAR, item = ACTION_MICROPHONE_CLICKED),
                )
                appStore.dispatch(VoiceInputRequested)
            }

            else -> {
                // no-op.
            }
        }
    }

    private fun openSearchOrUrl(text: String, navController: NavController) {
        val searchEngine = reconcileSelectedEngine()
        val isDefaultEngine = searchEngine?.id == browserStore.state.search.selectedOrDefaultSearchEngine?.id
        val newTab = if (settings.enableHomepageAsNewTab) {
            false
        } else {
            appStore.state.searchState.sourceTabId == null
        }

        navController.navigate(
            NavGraphDirections.actionGlobalBrowser(),
        )

        components.useCases.fenixBrowserUseCases.loadUrlOrSearch(
            searchTermOrURL = text,
            newTab = newTab,
            forceSearch = !isDefaultEngine,
            private = appStore.state.mode.isPrivate,
            searchEngine = searchEngine,
        )

        if (text.isUrl() || searchEngine == null) {
            Events.enteredUrl.record(Events.EnteredUrlExtra(autocomplete = false))
        } else {
            val searchAccessPoint = when (appStore.state.searchState.searchAccessPoint) {
                MetricsUtils.Source.NONE -> MetricsUtils.Source.ACTION
                else -> appStore.state.searchState.searchAccessPoint
            }

            MetricsUtils.recordSearchMetrics(
                searchEngine,
                isDefaultEngine,
                searchAccessPoint,
                components.nimbus.events,
            )
        }

        browserStore.dispatch(EngagementFinished(abandoned = false))
    }

    private fun refreshConfigurationAfterSearchEngineChange(
        store: Store<BrowserToolbarState, BrowserToolbarAction>,
        searchEngine: SearchEngine?,
    ) {
        updateSearchSelectorMenu(store, searchEngine, browserStore.state.search.searchEngineShortcuts)
        updateAutocompletions(store, store.state.editState.query)
        updateToolbarHint(store, searchEngine)
    }

    private fun updateToolbarHint(
        store: Store<BrowserToolbarState, BrowserToolbarAction>,
        engine: SearchEngine?,
    ) {
        val defaultEngine = browserStore.state.search.selectedOrDefaultSearchEngine
        val hintRes = engine.toolbarHintRes(defaultEngine)
        store.dispatch(HintUpdated(hintRes))
    }

    /**
     * Synchronously update the toolbar with a new search selector.
     */
    private fun updateSearchSelectorMenu(
        store: Store<BrowserToolbarState, BrowserToolbarAction>,
        selectedSearchEngine: SearchEngine?,
        searchEngineShortcuts: List<SearchEngine>,
    ) {
        val searchSelector = buildSearchSelector(
            selectedSearchEngine,
            searchEngineShortcuts,
            uiContext.resources,
        )
        store.dispatch(
            SearchActionsStartUpdated(
                when (searchSelector == null) {
                    true -> emptyList()
                    else -> listOf(searchSelector)
                },
            ),
        )
    }

    private fun buildAutocompleteProvidersList(selectedSearchEngine: SearchEngine?) = when (selectedSearchEngine?.id) {
        browserStore.state.search.selectedOrDefaultSearchEngine?.id -> listOfNotNull(
            when (settings.shouldShowHistorySuggestions) {
                true -> components.core.historyStorage
                false -> null
            },
            when (settings.shouldShowBookmarkSuggestions) {
                true -> components.core.bookmarksStorage
                false -> null
            },
            components.core.domainsAutocompleteProvider,
        )

        TABS_SEARCH_ENGINE_ID -> listOf(
            components.core.sessionAutocompleteProvider,
            components.backgroundServices.syncedTabsAutocompleteProvider,
        )

        BOOKMARKS_SEARCH_ENGINE_ID -> listOf(
            components.core.bookmarksStorage,
        )

        HISTORY_SEARCH_ENGINE_ID -> listOf(
            components.core.historyStorage,
        )

        else -> emptyList()
    }

    private fun updateAutocompletions(
        store: Store<BrowserToolbarState, BrowserToolbarAction>,
        query: BrowserToolbarQuery,
    ) {
        updateAutocompleteJob?.cancelChildren()

        // Update suggestions only if feature is not disabled and user is not backspacing.
        val shouldCheckForSuggestions = settings.shouldAutocompleteInAwesomebar && query.current.isNotEmpty()
        val isBackspacing = query.previous?.startsWith(query.current) == true &&
                query.previous?.length == query.current.length + 1
        if (shouldCheckForSuggestions && !isBackspacing) {
            updateAutocompleteJob = scope.launch {
                store.dispatch(
                    BrowserEditToolbarAction.AutocompleteSuggestionUpdated(
                        withContext(autocompleteDispatcher) {
                            fetchAutocomplete(
                                buildAutocompleteProvidersList(reconcileSelectedEngine()),
                                store.state.editState.query.current,
                            )?.also {
                                components.core.engine.speculativeConnect(it.url)
                            }
                        },
                    ),
                )
            }
        } else {
            store.dispatch(BrowserEditToolbarAction.AutocompleteSuggestionUpdated(null))
        }
    }

    @VisibleForTesting
    internal suspend fun fetchAutocomplete(
        autocompleteProviders: List<AutocompleteProvider>,
        input: String,
    ): AutocompleteResult? {
        if (autocompleteProviders.isEmpty()) return null

        return autocompleteProviders.firstNotNullOfOrNull { it.getAutocompleteSuggestion(input) }
    }

    private fun syncCurrentSearchEngine(store: Store<BrowserToolbarState, BrowserToolbarAction>) {
        syncCurrentSearchEngineJob?.cancel()
        syncCurrentSearchEngineJob = appStore.observeWhileActive {
            distinctUntilChangedBy { it.searchState.selectedSearchEngine?.searchEngine }
                .collect {
                    it.searchState.selectedSearchEngine?.let {
                        refreshConfigurationAfterSearchEngineChange(store, it.searchEngine)
                    }
                }
        }
    }

    private fun syncAvailableEngines(store: Store<BrowserToolbarState, BrowserToolbarAction>) {
        syncAvailableSearchEnginesJob?.cancel()
        syncAvailableSearchEnginesJob = browserStore.observeWhileActive {
            distinctUntilChangedBy { it.search.searchEngineShortcuts }
                .collect {
                    refreshConfigurationAfterSearchEngineChange(
                        store = store,
                        searchEngine = reconcileSelectedEngine(),
                    )
                }
        }
    }

    private fun reconcileSelectedEngine(): SearchEngine? =
        appStore.state.searchState.selectedSearchEngine?.searchEngine
            ?: browserStore.state.search.selectedOrDefaultSearchEngine

    private fun updateSearchEndPageActions(
        store: Store<BrowserToolbarState, BrowserToolbarAction>,
        selectedSearchEngine: SearchEngine? = reconcileSelectedEngine(),
    ) = store.dispatch(
        SearchActionsEndUpdated(
            buildSearchEndPageActions(
                store.state.editState.query.current,
                selectedSearchEngine,
            ),
        ),
    )

    private fun buildSearchEndPageActions(
        queryText: String,
        selectedSearchEngine: SearchEngine?,
    ): List<Action> = buildList {
        val isValidSearchEngine = selectedSearchEngine?.isGeneral == true ||
                selectedSearchEngine?.type == CUSTOM

        if (settings.shouldShowVoiceSearch && isSpeechRecognitionAvailable()) {
            add(
                ActionButtonRes(
                    drawableResId = iconsR.drawable.mozac_ic_microphone_24,
                    contentDescription = R.string.voice_search_content_description,
                    onClick = VoiceSearchButtonClicked,
                ),
            )
        }
        if (queryText.isNotEmpty()) {
            add(
                ActionButtonRes(
                    drawableResId = iconsR.drawable.mozac_ic_cross_circle_fill_24,
                    contentDescription = toolbarR.string.mozac_clear_button_description,
                    state = ActionButton.State.DEFAULT,
                    onClick = ClearSearchClicked,
                ),
            )
        } else if (false) {
            add(
                ActionButtonRes(
                    drawableResId = iconsR.drawable.mozac_ic_qr_code_24,
                    contentDescription = qrR.string.mozac_feature_qr_scanner,
                    state = ActionButton.State.DEFAULT,
                    onClick = QrScannerClicked,
                ),
            )
        }
    }

    private fun observeQrScannerInput(store: Store<BrowserToolbarState, BrowserToolbarAction>) {
        observeQRScannerInputJob = null
        observeQRScannerInputJob = appStore.observeWhileActive {
            distinctUntilChangedBy { it.qrScannerState.lastScanData }
                .collect {
                    if (it.qrScannerState.lastScanData?.isNotEmpty() == true) {
                        observeQRScannerInputJob?.cancel()

                        appStore.dispatch(AppAction.QrScannerAction.QrScannerInputConsumed)
                        store.dispatch(
                            SearchQueryUpdated(
                                BrowserToolbarQuery(it.qrScannerState.lastScanData),
                            ),
                        )
                        components.useCases.fenixBrowserUseCases.loadUrlOrSearch(
                            searchTermOrURL = it.qrScannerState.lastScanData,
                            newTab = appStore.state.searchState.sourceTabId == null,
                            flags = EngineSession.LoadUrlFlags.external(),
                            private = appStore.state.mode.isPrivate,
                        )
                        navController.navigate(R.id.action_global_browser)
                    }
                }
        }
    }

    private fun observeVoiceInputResults(
        store: Store<BrowserToolbarState, BrowserToolbarAction>,
    ) {
        observeVoiceInputJob?.cancel()
        observeVoiceInputJob = appStore.observeWhileActive {
            map { it.voiceSearchState.voiceInputResult }
                .distinctUntilChanged()
                .collect { voiceInputResult ->
                    if (!voiceInputResult.isNullOrEmpty()) {
                        store.dispatch(
                            SearchQueryUpdated(
                                query = BrowserToolbarQuery(voiceInputResult),
                                isQueryPrefilled = true,
                            ),
                        )
                        store.dispatch(CommitUrl(voiceInputResult))
                        appStore.dispatch(VoiceInputRequestCleared)
                    }
                }
        }
    }

    @VisibleForTesting
    internal fun isSpeechRecognitionAvailable() =
        Intent(RecognizerIntent.ACTION_RECOGNIZE_SPEECH)
            .resolveActivity(uiContext.packageManager) != null

    private inline fun <S : State, A : MVIAction> Store<S, A>.observeWhileActive(
        crossinline observe: suspend (Flow<S>.() -> Unit),
    ): Job = scope.launch { flow().observe() }

    /**
     * Static functionalities of the [BrowserToolbarSearchMiddleware].
     */
    companion object {
        /**
         * Builds a [SearchSelectorAction] to be shown in the [BrowserToolbar].
         *
         * @param selectedSearchEngine The currently selected search engine.
         * @param searchEngineShortcuts The list of search engines available for selection.
         * @param resources [Resources] Used for accessing application resources.
         */
        fun buildSearchSelector(
            selectedSearchEngine: SearchEngine?,
            searchEngineShortcuts: List<SearchEngine>,
            resources: Resources,
        ): SearchSelectorAction? {
            if (selectedSearchEngine == null) {
                return null
            }

            val menuItems = buildList<BrowserToolbarMenuItem> {
                add(
                    BrowserToolbarMenuButton(
                        icon = null,
                        text = MenuItemStringResText(R.string.search_header_menu_item_2),
                        contentDescription = MenuItemDescriptionRes(R.string.search_header_menu_item_2),
                        onClick = null,
                    ),
                )
                val searchEngines = searchEngineShortcuts.filter { it.type != APPLICATION }
                if (searchEngines.isNotEmpty()) {
                    addAll(searchEngines.toToolbarMenuItems(resources))
                    add(BrowserToolbarMenuDivider)
                }

                val applicationSearchEngines = searchEngineShortcuts.filter { it.type == APPLICATION }
                if (applicationSearchEngines.isNotEmpty()) {
                    addAll(applicationSearchEngines.toToolbarMenuItems(resources))
                    add(BrowserToolbarMenuDivider)
                }

                add(
                    BrowserToolbarMenuButton(
                        icon = MenuItemIconRes(iconsR.drawable.mozac_ic_settings_24),
                        text = MenuItemStringResText(R.string.search_settings_menu_item),
                        contentDescription = MenuItemDescriptionRes(R.string.search_settings_menu_item),
                        onClick = SearchSettingsItemClicked,
                    ),
                )
            }

            return SearchSelectorAction(
                icon = SearchSelectorIcon(
                    drawable = selectedSearchEngine.icon.toDrawable(resources),
                    shouldTint = selectedSearchEngine.type == APPLICATION,
                ),
                contentDescription = SearchSelectorDescription(
                    resources.getString(
                        R.string.search_engine_selector_content_description,
                        selectedSearchEngine.name,
                    ),
                ),
                menu = BrowserToolbarMenu { menuItems },
                onClick = SearchSelectorClicked,
            )
        }

        private fun List<SearchEngine>.toToolbarMenuItems(
            resources: Resources,
        ) = map { searchEngine ->
            BrowserToolbarMenuButton(
                icon = MenuItemIcon(
                    drawable = searchEngine.icon.toDrawable(resources),
                    shouldTint = searchEngine.type == APPLICATION,
                ),
                text = MenuItemStringText(searchEngine.name),
                contentDescription = MenuItemStringDescription(searchEngine.name),
                onClick = SearchSelectorItemClicked(searchEngine),
            )
        }

        private const val AUTOCOMPLETE_QUERY_THREADS = 3
        private const val AUTOCOMPLETE_THREADS_FACTORY_NAME = "BrowserToolbarSearchMiddleware"
        private val defaultAutocompleteDispatcher by lazy {
            SupervisorJob() + Executors.newFixedThreadPool(
                AUTOCOMPLETE_QUERY_THREADS,
                NamedThreadFactory(AUTOCOMPLETE_THREADS_FACTORY_NAME),
            ).asCoroutineDispatcher() + CoroutineExceptionHandler { _, throwable ->
                Logger(AUTOCOMPLETE_THREADS_FACTORY_NAME)
                    .error("Error while processing autocomplete input", throwable)
            }
        }
    }
}
