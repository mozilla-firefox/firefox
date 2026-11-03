/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.home.toolbar

import android.content.Context
import android.content.Intent
import android.speech.RecognizerIntent
import androidx.annotation.VisibleForTesting
import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.core.Easing
import androidx.compose.animation.core.tween
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ColumnScope
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.material3.ColorScheme
import androidx.compose.material3.MaterialTheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.semantics.testTag
import androidx.compose.ui.semantics.testTagsAsResourceId
import androidx.navigation.NavController
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import mozilla.components.browser.state.action.AwesomeBarAction
import mozilla.components.browser.state.ext.getUrl
import mozilla.components.browser.state.selector.findTab
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.compose.base.utils.BackInvokedHandler
import mozilla.components.compose.browser.toolbar.BrowserToolbar
import mozilla.components.compose.browser.toolbar.store.BrowserEditToolbarAction.SearchQueryUpdated
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarAction.ToolbarGravityUpdated
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarStore
import mozilla.components.compose.browser.toolbar.store.ToolbarGravity
import mozilla.components.compose.browser.toolbar.store.ToolbarGravity.Bottom
import mozilla.components.compose.browser.toolbar.store.ToolbarGravity.Top
import mozilla.components.compose.browser.toolbar.ui.BrowserToolbarQuery
import mozilla.components.lib.state.ext.observeAsComposableState
import mozilla.components.support.ktx.util.URLStringUtils
import org.mozilla.fenix.R
import org.mozilla.fenix.browser.browsingmode.BrowsingModeManager
import org.mozilla.fenix.components.AppStore
import org.mozilla.fenix.components.appstate.AppAction.SearchAction.SearchEnded
import org.mozilla.fenix.components.appstate.AppAction.SearchAction.SearchStarted
import org.mozilla.fenix.components.appstate.VoiceSearchAction.VoiceInputRequested
import org.mozilla.fenix.components.metrics.MetricsUtils
import org.mozilla.fenix.components.toolbar.TopDividerColumn
import org.mozilla.fenix.theme.FirefoxTheme
import org.mozilla.fenix.utils.Settings
import org.mozilla.fenix.wallpapers.WallpaperTheme

// Speculative delay for putting the toolbar in edit mode after an initial voice search request.
@VisibleForTesting internal const val EDIT_TOOLBAR_DELAY_AFTER_VOICE_REQUEST = 1_000L

/**
 * A wrapper over the [BrowserToolbar] composable to allow for extra customisation.
 *
 * @param context [Context] used for various system interactions.
 * @param navController [NavController] to use for navigating to other in-app destinations.
 * @param toolbarStore [BrowserToolbarStore] containing the composable toolbar state.
 * @param appStore [AppStore] to sync from.
 * @param browserStore [BrowserStore] to sync from.
 * @param browsingModeManager [BrowsingModeManager] Manager holding current state of whether the browser is in private
 *   mode or not.
 * @param settings [Settings] for querying various application settings.
 * @param directToSearchConfig [DirectToSearchConfig] configuration for starting with the toolbar in search mode.
 * @param coroutineScope Coroutine scope used for delaying actions.
 * @param tabStripContent [Composable] as the tab strip content to be displayed together with this toolbar.
 * @param searchSuggestionsContent [Composable] as the search suggestions content to be displayed together with this
 *   toolbar.
 * @param navigationBarContent [Composable] content for the navigation bar.
 */
@Suppress("LongParameterList")
internal class HomeToolbarComposable(
    private val context: Context,
    private val navController: NavController,
    private val toolbarStore: BrowserToolbarStore,
    private val appStore: AppStore,
    private val browserStore: BrowserStore,
    private val browsingModeManager: BrowsingModeManager,
    private val settings: Settings,
    private val directToSearchConfig: DirectToSearchConfig,
    private val coroutineScope: CoroutineScope,
    private val tabStripContent: @Composable () -> Unit,
    private val searchSuggestionsContent: @Composable (Modifier) -> Unit,
    private val navigationBarContent: (@Composable () -> Unit)?,
) : FenixHomeToolbar {
    private val addressBarVisibility = mutableStateOf(true)

    init {
        // Reset the toolbar visibility & position whenever coming back to the home screen
        // like after changing the toolbar position in settings.
        toolbarStore.dispatch(ToolbarGravityUpdated(buildToolbarGravityConfig()))
    }

    @Composable
    private fun DefaultToolbar() {
        val isSearching = toolbarStore.observeAsComposableState { it.isEditMode() }.value
        val isSearchActive = appStore.observeAsComposableState { it.searchState.isSearchActive }.value
        val isPrivateMode = browsingModeManager.mode.isPrivate
        // Tint the browser action icons outside the address bar (tab counter, menu) with the
        // wallpaper's text color. The page actions inside the address bar (e.g. voice search) keep
        // the default color so they stay legible on the address bar background.
        val wallpaperTextColor = WallpaperTheme.onWallpaper.takeIf { !isPrivateMode }

        BackInvokedHandler(isSearching) {
            val sourceTabId = appStore.state.searchState.sourceTabId
            if (sourceTabId != null) {
                navController.navigate(R.id.browserFragment)
            }
            appStore.dispatch(SearchEnded)
            browserStore.dispatch(AwesomeBarAction.EngagementFinished(abandoned = true))
        }

        FirefoxTheme {
            MaterialTheme(colorScheme = toolbarColors(isSearching = isSearchActive)) {
                ToolbarContent(wallpaperTextColor = wallpaperTextColor)
            }
        }
    }

    /** Colors of the toolbar and of the search screen shown together with it. */
    @Composable
    private fun toolbarColors(isSearching: Boolean): ColorScheme {
        val queryWasPrefilled =
            toolbarStore
                .observeAsComposableState {
                    it.editState.queryWasPrefilled
                }
                .value
        val currentQuery = toolbarStore.observeAsComposableState { it.editState.query.current }.value
        return homepageToolbarColors(
            isPrivateMode = browsingModeManager.mode.isPrivate,
            shouldUseEdgeToEdgeColors =
                if (settings.enableHomepageTrendingRecentSearch) {
                    !isSearching
                } else {
                    !isSearching || (currentQuery.isEmpty() && !queryWasPrefilled)
                },
        )
    }

    @Composable
    private fun ToolbarContent(wallpaperTextColor: Color?) {
        val shouldUseBottomToolbar = remember { settings.shouldUseBottomToolbar }
        val shouldShowTabStrip = remember {
            if (shouldUseBottomToolbar) {
                settings.shouldShowTabStripAtBottom
            } else {
                settings.shouldShowTabStripAtTop
            }
        }
        val isSearchActive = appStore.observeAsComposableState { it.searchState.isSearchActive }.value
        val isTabStripAtBottom =
            remember { shouldUseBottomToolbar && settings.shouldShowTabStripAtBottom } && !isSearchActive

        Column(
            modifier =
                Modifier.semantics {
                    testTagsAsResourceId = true
                    testTag = context.resources.getResourceName(R.id.composable_toolbar)
                }
        ) {
            if (shouldUseBottomToolbar) {
                searchSuggestionsContent(Modifier.weight(1f))
            }

            TopDividerColumn(showDivider = isTabStripAtBottom, modifier = Modifier.fillMaxWidth()) {
                if (shouldShowTabStrip) {
                    tabStripContent()
                }

                AddressBar(wallpaperTextColor = wallpaperTextColor)

                if (shouldUseBottomToolbar) {
                    navigationBarContent?.invoke()
                }
            }

            if (!shouldUseBottomToolbar) {
                searchSuggestionsContent(Modifier.weight(1f))
            }
        }
    }

    @Composable
    private fun ColumnScope.AddressBar(wallpaperTextColor: Color?) {
        val isAddressBarVisible = remember { addressBarVisibility }
        val isSearchActive = appStore.observeAsComposableState { it.searchState.isSearchActive }.value
        val hasBottomStrip =
            settings.shouldUseBottomToolbar &&
                ((settings.shouldShowTabStripAtBottom && !isSearchActive) || settings.shouldShowTabGroupsStrip)

        Box {
            if (settings.isHomepageMiddleSearchEnabled) {
                BrowserSimpleToolbar(toolbarStore, appStore)
            }

            this@AddressBar.AnimatedVisibility(
                visible = isAddressBarVisible.value || appStore.state.searchState.isSearchActive,
                enter =
                    fadeIn(
                        animationSpec =
                            tween(
                                durationMillis = 250,
                                easing = Easing { fraction -> fraction * fraction },
                            )
                    ),
                exit =
                    fadeOut(
                        animationSpec =
                            tween(
                                durationMillis = 250,
                                easing = Easing { fraction -> 1f - (1f - fraction) * (1f - fraction) },
                            )
                    ),
            ) {
                BrowserToolbar(
                    store = toolbarStore,
                    browserActionsColor = wallpaperTextColor,
                    showDivider = !hasBottomStrip,
                )
            }
        }
    }

    @Composable
    override fun Content() {
        DefaultToolbar()
    }

    @Composable
    override fun searchBackgroundColor(): Color? {
        val isSearchActive = appStore.observeAsComposableState { it.searchState.isSearchActive }.value
        return if (isSearchActive) toolbarColors(isSearching = true).surface else null
    }

    override fun build(middleSearchEnabled: Boolean) {
        configureStartingInSearchMode()
        updateAddressBarVisibility(!middleSearchEnabled)
    }

    override fun updateAddressBarVisibility(isVisible: Boolean) {
        addressBarVisibility.value = isVisible
    }

    private fun buildToolbarGravityConfig(): ToolbarGravity =
        when (settings.shouldUseBottomToolbar) {
            true -> Bottom
            false -> Top
        }

    private fun configureStartingInSearchMode() {
        if (shouldStartToVoiceSearch()) {
            handleVoiceSearchRequest()
        } else if (directToSearchConfig.startSearch) {
            handleTypedSearchRequest()
        }
    }

    private fun handleVoiceSearchRequest() {
        appStore.dispatch(VoiceInputRequested)

        if (directToSearchConfig.startSearch) {
            coroutineScope.launch {
                // We need to ensure the toolbar is in edit mode to handle the result of the voice search
                // but this might start showing the keyboard while the voice prompt is showing.
                // Add a speculative delay to putting the toolbar in edit mode to ensure the voice prompt
                // gets shown first which would prevent the keyboard also showing.
                // Alternative approaches to be investigated in bug 2025269.
                delay(EDIT_TOOLBAR_DELAY_AFTER_VOICE_REQUEST)

                handleStartingSearch(directToSearchConfig)
            }
        }
    }

    private fun handleTypedSearchRequest() {
        handleStartingSearch(directToSearchConfig)

        if (directToSearchConfig.sessionId != null && !settings.showAddressBarInFocusMode) {
            browserStore.state.findTab(directToSearchConfig.sessionId)?.let {
                toolbarStore.dispatch(
                    SearchQueryUpdated(
                        query = BrowserToolbarQuery(URLStringUtils.toLosslessEditableUrl(it.getUrl().orEmpty())),
                        isQueryPrefilled = true,
                    )
                )
            }
        }
    }

    private fun handleStartingSearch(directToSearchConfig: DirectToSearchConfig) {
        appStore.dispatch(
            SearchStarted(
                tabId = directToSearchConfig.sessionId,
                source = directToSearchConfig.source,
            )
        )
    }

    private fun shouldStartToVoiceSearch() = directToSearchConfig.startVoiceSearch && isSpeechRecognitionAvailable()

    private fun isSpeechRecognitionAvailable() =
        Intent(RecognizerIntent.ACTION_RECOGNIZE_SPEECH).resolveActivity(context.packageManager) != null

    /** Static configuration and properties of [HomeToolbarComposable]. */
    companion object {
        /**
         * Configuration for starting with the toolbar in search mode.
         *
         * @property startSearch Whether to start in search mode. Defaults to `false`.
         * @property startVoiceSearch Whether to start in voice search mode. Defaults to `false`.
         * @property sessionId The session ID of the current session with details of which to start search. Defaults to
         *   `null`.
         * @property source The application feature from where a new search was started.
         */
        data class DirectToSearchConfig(
            val startSearch: Boolean = false,
            val startVoiceSearch: Boolean = false,
            val sessionId: String? = null,
            val source: MetricsUtils.Source = MetricsUtils.Source.NONE,
        )
    }
}
