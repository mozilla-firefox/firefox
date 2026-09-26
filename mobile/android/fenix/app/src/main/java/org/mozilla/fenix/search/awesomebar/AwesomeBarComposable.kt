/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.search.awesomebar

import androidx.compose.foundation.background
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.isImeVisible
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.layout.wrapContentWidth
import androidx.compose.material3.MaterialTheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.derivedStateOf
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.platform.LocalFocusManager
import androidx.compose.ui.platform.LocalSoftwareKeyboardController
import androidx.compose.ui.platform.LocalView
import androidx.compose.ui.unit.dp
import androidx.core.graphics.toColorInt
import androidx.fragment.app.Fragment
import androidx.lifecycle.coroutineScope
import androidx.navigation.NavController
import mozilla.components.browser.state.action.AwesomeBarAction
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.compose.base.theme.layout.AcornWindowSize
import mozilla.components.compose.browser.awesomebar.AwesomeBar
import mozilla.components.compose.browser.awesomebar.AwesomeBarOrientation
import mozilla.components.compose.browser.awesomebar.internal.CurrentTabDetailsInteractions.CopyClicked
import mozilla.components.compose.browser.awesomebar.internal.CurrentTabDetailsInteractions.DetailsClicked
import mozilla.components.compose.browser.awesomebar.internal.CurrentTabDetailsInteractions.EditClicked
import mozilla.components.compose.browser.awesomebar.internal.CurrentTabDetailsInteractions.ShareClicked
import mozilla.components.compose.browser.toolbar.store.BrowserEditToolbarAction.SearchQueryUpdated
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarStore
import mozilla.components.compose.browser.toolbar.ui.BrowserToolbarQuery
import mozilla.components.lib.state.ext.observeAsComposableState
import mozilla.components.lib.state.helpers.StoreProvider.Companion.fragmentStore
import mozilla.components.support.ktx.android.view.hideKeyboard
import org.mozilla.fenix.HomeActivity
import org.mozilla.fenix.R
import org.mozilla.fenix.browser.browsingmode.BrowsingMode
import org.mozilla.fenix.components.AppStore
import org.mozilla.fenix.components.Components
import org.mozilla.fenix.components.appstate.AppAction.SearchAction.SearchEnded
import org.mozilla.fenix.components.metrics.MetricsUtils
import org.mozilla.fenix.ext.components
import org.mozilla.fenix.ext.getRootView
import org.mozilla.fenix.home.toolbar.edgeToEdgeClipboardBarBackground
import org.mozilla.fenix.search.BrowserStoreToFenixSearchMapperMiddleware
import org.mozilla.fenix.search.BrowserToolbarToFenixSearchMapperMiddleware
import org.mozilla.fenix.search.FenixSearchMiddleware
import org.mozilla.fenix.search.SearchFragmentAction
import org.mozilla.fenix.search.SearchFragmentAction.CopyCurrentWebsiteDetailsClicked
import org.mozilla.fenix.search.SearchFragmentAction.EditCurrentWebsiteDetailsClicked
import org.mozilla.fenix.search.SearchFragmentAction.ReloadCurrentWebsiteClicked
import org.mozilla.fenix.search.SearchFragmentAction.ShareCurrentWebsiteDetailsClicked
import org.mozilla.fenix.search.SearchFragmentAction.SuggestionClicked
import org.mozilla.fenix.search.SearchFragmentAction.SuggestionSelected
import org.mozilla.fenix.search.SearchFragmentStore
import org.mozilla.fenix.search.createInitialSearchFragmentState
import org.mozilla.fenix.settings.SupportUtils

private const val MATERIAL_DESIGN_SCRIM = "#52000000"
private const val AWESOMEBAR_MAX_WIDTH = 600

/**
 * Cap the width of the awesomebar content to [AcornWindowSize.Small] and center it in the available space as a staged
 * rollout change.
 */
private fun Modifier.awesomeBarContentWidth(useAddressBarFocusMode: Boolean) =
    if (useAddressBarFocusMode) {
        fillMaxWidth().wrapContentWidth(align = Alignment.CenterHorizontally).widthIn(max = AWESOMEBAR_MAX_WIDTH.dp)
    } else {
        fillMaxWidth()
    }

/**
 * Wrapper over a [Composable] to show search suggestions, responsible for its setup.
 *
 * @param activity [HomeActivity] providing the ability to open URLs and querying the current browsing mode.
 * @param fragment [Fragment] to the lifecycle of which long running operations and objects will be tied to.
 * @param modifier [Modifier] to be applied to the [Composable].
 * @param components [Components] for accessing other functionalities of the application.
 * @param appStore [AppStore] for accessing the current application state.
 * @param browserStore [BrowserStore] for accessing the current browser state.
 * @param toolbarStore [BrowserToolbarStore] for accessing the current toolbar state.
 * @param navController [NavController] for navigating to other destinations in the application.
 * @param tabId [String] Id of the current tab for which a new search was started.
 * @param showScrimWhenNoSuggestions Whether to show a scrim when no suggestions are available.
 * @param searchAccessPoint Where search was started from.
 * @param isEdgeToEdgeBackgroundEnabled Whether the Edge2Edge background is enabled.
 */
class AwesomeBarComposable(
    private val activity: HomeActivity,
    private val fragment: Fragment,
    private val modifier: Modifier,
    private val components: Components,
    private val appStore: AppStore,
    private val browserStore: BrowserStore,
    private val toolbarStore: BrowserToolbarStore,
    private val navController: NavController,
    private val tabId: String? = null,
    private val showScrimWhenNoSuggestions: Boolean = false,
    private val searchAccessPoint: MetricsUtils.Source = MetricsUtils.Source.NONE,
    private val isEdgeToEdgeBackgroundEnabled: Boolean = false,
) {
    private val searchStore by initializeSearchStore()

    /**
     * [Composable] fully integrated with [BrowserStore] and [BrowserToolbarStore] that will show search suggestions
     * whenever the users edits the current query in the toolbar.
     */
    @OptIn(ExperimentalLayoutApi::class) // for WindowInsets.isImeVisible
    @Suppress("LongMethod", "CyclomaticComplexMethod", "CognitiveComplexMethod")
    @Composable
    fun SearchSuggestions() {
        val deleteHistoryDelegate =
            remember(activity.getRootView(), searchStore) {
                activity.getRootView()?.let {
                    DeleteHistoryEntryDelegate(it, it.context.components, searchStore)
                }
            }
        val isSearchActive = appStore.observeAsComposableState { it.searchState.isSearchActive }.value
        val state = searchStore.observeAsComposableState { it }.value
        val orientation by
            remember(state.searchSuggestionsOrientedAtBottom) {
                derivedStateOf {
                    when (searchStore.state.searchSuggestionsOrientedAtBottom) {
                        true -> AwesomeBarOrientation.BOTTOM
                        false -> AwesomeBarOrientation.TOP
                    }
                }
            }
        val shouldShowClipboardBar by
            remember(
                state.showClipboardSuggestions,
                state.query,
                state.clipboardHasUrl,
            ) {
                derivedStateOf {
                    state.showClipboardSuggestions && state.query.isEmpty() && state.clipboardHasUrl
                }
            }
        val clipboardBarBackground =
            edgeToEdgeClipboardBarBackground(
                shouldUseEdgeToEdgeColors = isEdgeToEdgeBackgroundEnabled,
                isPrivateMode = activity.browsingModeManager.mode == BrowsingMode.Private,
            )
        val showCurrentTabData = remember { components.settings.showAddressBarInFocusMode }
        val view = LocalView.current
        val focusManager = LocalFocusManager.current
        val keyboardController = LocalSoftwareKeyboardController.current

        LaunchedEffect(isSearchActive) {
            if (!isSearchActive) {
                focusManager.clearFocus()
                keyboardController?.hide()
            } else {
                val hasUrl = components.clipboardHandler.containsURL()
                searchStore.dispatch(SearchFragmentAction.UpdateClipboardHasUrl(hasUrl))
            }
        }

        if (isSearchActive && shouldShowClipboardBar && orientation == AwesomeBarOrientation.TOP) {
            val url = components.clipboardHandler.extractURL()

            ClipboardSuggestionBar(
                modifier = Modifier.awesomeBarContentWidth(showCurrentTabData),
                shouldUseBottomToolbar = components.settings.shouldUseBottomToolbar,
                backgroundColor = clipboardBarBackground,
                onClick = {
                    url?.let {
                        toolbarStore.dispatch(
                            SearchQueryUpdated(query = BrowserToolbarQuery(url), isQueryPrefilled = true)
                        )
                    }
                },
            )
        }

        if (isSearchActive) {
            if (state.showSearchSuggestionsHint) {
                PrivateSuggestionsCard(
                    modifier = Modifier.awesomeBarContentWidth(showCurrentTabData),
                    onSearchSuggestionsInPrivateModeAllowed = {
                        components.settings.shouldShowSearchSuggestionsInPrivate = true
                        components.settings.showSearchSuggestionsInPrivateOnboardingFinished = true
                        searchStore.dispatch(SearchFragmentAction.SetShowSearchSuggestions(true))
                        searchStore.dispatch(SearchFragmentAction.AllowSearchSuggestionsInPrivateModePrompt(false))
                        searchStore.dispatch(SearchFragmentAction.PrivateSuggestionsCardAccepted)
                    },
                    onSearchSuggestionsInPrivateModeBlocked = {
                        components.settings.shouldShowSearchSuggestionsInPrivate = false
                        components.settings.showSearchSuggestionsInPrivateOnboardingFinished = true
                        searchStore.dispatch(SearchFragmentAction.AllowSearchSuggestionsInPrivateModePrompt(false))
                    },
                    onLearnMoreClick = {
                        components.useCases.fenixBrowserUseCases.loadUrlOrSearch(
                            searchTermOrURL =
                                SupportUtils.getGenericSumoURLForTopic(SupportUtils.SumoTopic.SEARCH_SUGGESTION),
                            newTab = appStore.state.searchState.sourceTabId == null,
                            private = true,
                        )
                        navController.navigate(R.id.browserFragment)
                    },
                )
            }
            if (state.shouldShowSearchSuggestions) {
                Box(
                    modifier =
                        modifier.background(MaterialTheme.colorScheme.surface).fillMaxSize().pointerInput(
                            WindowInsets.isImeVisible
                        ) {
                            detectTapGestures(
                                onPress = {
                                    view.hideKeyboard()
                                },
                                onTap = {
                                    focusManager.clearFocus()
                                    appStore.dispatch(SearchEnded)
                                },
                            )
                        }
                ) {
                    val currentTabDetailsToShow =
                        remember(state.query.isBlank()) {
                            when (state.query.isBlank() && showCurrentTabData) {
                                true -> state.currentTabData
                                else -> null
                            }
                        }

                    Box(modifier = Modifier.awesomeBarContentWidth(showCurrentTabData)) {
                        AwesomeBar(
                            text = state.query,
                            currentTabData = currentTabDetailsToShow,
                            providers = state.searchSuggestionsProviders,
                            hiddenSuggestions = state.hiddenSuggestions,
                            orientation = orientation,
                            onSuggestionClicked = { suggestion ->
                                searchStore.dispatch(SuggestionClicked(suggestion))
                            },
                            onAutoComplete = { suggestion ->
                                searchStore.dispatch(SuggestionSelected(suggestion))
                            },
                            onRemoveClicked = { suggestion ->
                                deleteHistoryDelegate?.handleDeletingHistoryEntry(suggestion)
                            },
                            onCurrentSiteDetailsInteraction = {
                                when (it) {
                                    ShareClicked -> searchStore.dispatch(ShareCurrentWebsiteDetailsClicked)
                                    CopyClicked -> searchStore.dispatch(CopyCurrentWebsiteDetailsClicked)
                                    EditClicked -> searchStore.dispatch(EditCurrentWebsiteDetailsClicked)
                                    DetailsClicked -> searchStore.dispatch(ReloadCurrentWebsiteClicked)
                                }
                            },
                            onVisibilityStateUpdated = {
                                browserStore.dispatch(AwesomeBarAction.VisibilityStateUpdated(it))
                            },
                            onScroll = { view.hideKeyboard() },
                            profiler = components.core.engine.profiler,
                        )
                    }
                }
            } else if (showScrimWhenNoSuggestions) {
                Spacer(
                    modifier =
                        modifier.background(Color(MATERIAL_DESIGN_SCRIM.toColorInt())).fillMaxSize().pointerInput(
                            WindowInsets.isImeVisible
                        ) {
                            detectTapGestures(
                                onPress = {
                                    keyboardController?.hide()
                                },
                                onTap = {
                                    focusManager.clearFocus()
                                    appStore.dispatch(SearchEnded)
                                },
                            )
                        }
                )
            }
        }

        if (isSearchActive && shouldShowClipboardBar && orientation == AwesomeBarOrientation.BOTTOM) {
            val url = components.clipboardHandler.extractURL()

            ClipboardSuggestionBar(
                modifier = Modifier.awesomeBarContentWidth(showCurrentTabData),
                shouldUseBottomToolbar = components.settings.shouldUseBottomToolbar,
                backgroundColor = clipboardBarBackground,
                onClick = {
                    url?.let {
                        toolbarStore.dispatch(
                            SearchQueryUpdated(query = BrowserToolbarQuery(url), isQueryPrefilled = true)
                        )
                    }
                },
            )
        }
    }

    private fun initializeSearchStore() =
        fragment.fragmentStore(
            createInitialSearchFragmentState(
                context = activity,
                components = components,
                tabId = tabId,
                pastedText = null,
                searchAccessPoint = searchAccessPoint,
            )
        ) {
            val lifecycleScope = fragment.viewLifecycleOwner.lifecycle.coroutineScope

            SearchFragmentStore(
                initialState = it,
                middleware =
                    listOf(
                        BrowserToolbarToFenixSearchMapperMiddleware(
                            toolbarStore = toolbarStore,
                            browsingModeManager = activity.browsingModeManager,
                            scope = lifecycleScope,
                            browserStore = browserStore,
                        ),
                        BrowserStoreToFenixSearchMapperMiddleware(
                            browserStore = browserStore,
                            scope = lifecycleScope,
                            appStore = components.appStore,
                        ),
                        FenixSearchMiddleware(
                            fragment = fragment,
                            engine = components.core.engine,
                            useCases = components.useCases,
                            nimbusComponents = components.nimbus,
                            settings = components.settings,
                            appStore = appStore,
                            browserStore = browserStore,
                            toolbarStore = toolbarStore,
                            navController = navController,
                            browsingModeManager = activity.browsingModeManager,
                            shareUseCases = components.useCases.shareUseCases,
                            clipboardHandler = components.clipboardHandler,
                        ),
                    ),
            )
        }
}
