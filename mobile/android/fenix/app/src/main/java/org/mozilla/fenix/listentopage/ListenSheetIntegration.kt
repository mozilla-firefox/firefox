/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.listentopage

import android.view.View
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.State
import androidx.compose.runtime.derivedStateOf
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.ComposeView
import androidx.compose.ui.platform.LocalViewConfiguration
import androidx.compose.ui.platform.ViewCompositionStrategy
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.isTraversalGroup
import androidx.compose.ui.semantics.semantics
import androidx.coordinatorlayout.widget.CoordinatorLayout
import androidx.core.view.isVisible
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import kotlin.math.abs
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.drop
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.flow.last
import kotlinx.coroutines.flow.take
import mozilla.components.browser.state.selector.selectedTab
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.concept.engine.EngineView
import mozilla.components.feature.listentopage.ArticleProgress
import mozilla.components.feature.listentopage.ListenAction
import mozilla.components.feature.listentopage.ListenState
import mozilla.components.feature.listentopage.ListenStore
import mozilla.components.feature.listentopage.PlayerMode
import mozilla.components.feature.listentopage.settings.ListenEnabledState
import mozilla.components.feature.listentopage.ui.ListenSheet
import mozilla.components.feature.listentopage.ui.ListenSheetUiState
import mozilla.components.feature.listentopage.ui.toListenSheetUiState
import mozilla.components.lib.state.ext.observeAsComposableState
import mozilla.components.support.base.feature.LifecycleAwareFeature
import org.mozilla.fenix.R
import org.mozilla.fenix.theme.FirefoxTheme

private val ListenState.isArticleReady: Boolean
    get() = tabId != null && languageTag != null

/**
 * This integration is responsible for showing or hiding the reader mode panel and the Listen to page media player, and
 * properly anchoring them to the browser.
 *
 * @param container The [CoordinatorLayout] the panel and the player are added to.
 * @param browserStore Used to tell whether reader mode is showing.
 * @param listenStore Used to tell whether there is an article to play.
 * @param engineView Used to tell how far the article is scrolled.
 * @param isAddressBarAtBottom Whether the address bar is at the bottom of the screen.
 * @param isEnabled Whether the user has the feature turned on. The listen button is only shown when it is
 *   [ListenEnabledState.Enabled].
 * @param onListenClicked Invoked when the user asks to listen to the article.
 * @param onCustomizeReaderViewClicked Invoked when the user asks for the reader view appearance controls.
 */
class ListenSheetIntegration(
    private val container: CoordinatorLayout,
    private val browserStore: BrowserStore,
    private val listenStore: ListenStore,
    private val engineView: EngineView,
    private val isAddressBarAtBottom: Boolean,
    private val isEnabled: StateFlow<ListenEnabledState>,
    private val onListenClicked: () -> Unit,
    private val onCustomizeReaderViewClicked: () -> Unit,
) : LifecycleAwareFeature {

    private var listenFeature: ComposeView? = null

    override fun start() {
        listenFeature?.let {
            // Set again because stop() disposes the composition while the view keeps its place in the container.
            it.setContent { ListenFeatureHost() }
            it.isVisible = true
            return
        }

        val view =
            ComposeView(container.context).apply {
                id = R.id.listenSheet
                importantForAccessibility = View.IMPORTANT_FOR_ACCESSIBILITY_YES
                layoutParams =
                    CoordinatorLayout.LayoutParams(
                            CoordinatorLayout.LayoutParams.MATCH_PARENT,
                            CoordinatorLayout.LayoutParams.WRAP_CONTENT,
                        )
                        .apply { behavior = ListenSheetBehavior(isAddressBarAtBottom = isAddressBarAtBottom) }
                setContent { ListenFeatureHost() }
                setViewCompositionStrategy(ViewCompositionStrategy.DisposeOnViewTreeLifecycleDestroyed)
            }
        listenFeature = view

        container.post {
            if (listenFeature === view) {
                container.addView(view)
                container.findViewById<View>(R.id.engineView)?.accessibilityTraversalAfter = view.id
            }
        }
    }

    override fun stop() {
        listenFeature?.apply {
            isVisible = false
            // The view stays so that a snackbar anchored to it still resolves.
            disposeComposition()
        }
    }

    @Composable
    private fun ListenFeatureHost() {
        val isReaderModeActive by browserStore.observeAsComposableState { it.selectedTab?.readerState?.active == true }

        if (!isReaderModeActive) {
            return
        }

        val selectedTabIdState = browserStore.observeAsComposableState { it.selectedTabId }
        val readyTabIdState = listenStore.observeAsComposableState { if (it.isArticleReady) it.tabId else null }
        val listenSheetUiState by listenStore.observeAsComposableState { it.toListenSheetUiState() }
        val isListeningEnabled by isEnabled.collectAsStateWithLifecycle()
        val articleProgressState = listenStore.observeAsComposableState { it.articleProgress }
        val shouldDisplayPlayer by remember {
            derivedStateOf {
                val s = readyTabIdState.value
                s != null && s == selectedTabIdState.value
            }
        }
        FirefoxTheme {
            ListenFeatureContent(
                shouldDisplayPlayer = shouldDisplayPlayer,
                isListeningEnabled = isListeningEnabled == ListenEnabledState.Enabled,
                uiState = listenSheetUiState,
                articleProgressState = articleProgressState,
                scrollPosition = engineView.verticalScrollPosition,
                onAction = listenStore::dispatch,
                onListenClicked = onListenClicked,
                onCustomizeReaderViewClicked = onCustomizeReaderViewClicked,
            )
        }
    }
}

/**
 * Shows the reader mode panel over an article, and replaces the panel with the ListenSheet media player controls once
 * the article audio is ready for playback.
 *
 * @param shouldDisplayPlayer Whether the audio of the article in the selected tab is ready for playback.
 * @param isListeningEnabled Whether the reader mode panel shows the button that reads the article out loud.
 * @param uiState Contains title, site and playback state needed for media player
 * @param articleProgressState Contains position and duration needed to show elapsed time, total time and progress in
 *   player.
 * @param scrollPosition How far the article is scrolled, in pixels, which collapses the player.
 * @param onAction Invoked to pass upwards a [ListenAction] in response to a UI event.
 * @param onListenClicked Invoked when the user asks to listen to the article.
 * @param onCustomizeReaderViewClicked Invoked when the user asks for the reader view appearance controls.
 */
@Suppress("LongParameterList")
@Composable
fun ListenFeatureContent(
    shouldDisplayPlayer: Boolean,
    isListeningEnabled: Boolean,
    uiState: ListenSheetUiState,
    articleProgressState: State<ArticleProgress>,
    scrollPosition: Flow<Float>,
    onAction: (ListenAction) -> Unit,
    onListenClicked: () -> Unit,
    onCustomizeReaderViewClicked: () -> Unit,
) {
    val modifierWithPadding =
        if (shouldDisplayPlayer) {
            Modifier.padding(FirefoxTheme.layout.space.static300)
        } else {
            Modifier.padding(
                horizontal = FirefoxTheme.layout.space.static200,
                vertical = FirefoxTheme.layout.space.static300,
            )
        }
    Box(
        modifier = Modifier.fillMaxWidth().then(modifierWithPadding),
        contentAlignment = Alignment.BottomEnd,
    ) {
        if (shouldDisplayPlayer) {
            ListenSheetContent(uiState, articleProgressState, scrollPosition, onAction)
        } else {
            ReaderModePanel(
                isListeningEnabled = isListeningEnabled,
                onListenClicked = onListenClicked,
                onCustomizeReaderViewClicked = onCustomizeReaderViewClicked,
            )
        }
    }
}

/** Shows media player controls for Listen To Page feature */
@Composable
private fun ListenSheetContent(
    uiState: ListenSheetUiState,
    articleProgressState: State<ArticleProgress>,
    scrollPosition: Flow<Float>,
    onAction: (ListenAction) -> Unit,
) {
    val touchSlop = LocalViewConfiguration.current.touchSlop
    val mode = uiState.mode
    LaunchedEffect(scrollPosition, touchSlop, mode) {
        // The flow replays the last known position, so the first new one is where the scroll is measured from.
        val anchor = scrollPosition.take(2).last()
        scrollPosition.drop(1).first { scrollY ->
            val isArticleAtTop = scrollY <= touchSlop
            when (mode) {
                PlayerMode.Expanded -> !isArticleAtTop && abs(scrollY - anchor) > touchSlop
                PlayerMode.Compact -> isArticleAtTop
            }
        }
        onAction(ListenAction.ModeChanged(if (mode == PlayerMode.Expanded) PlayerMode.Compact else PlayerMode.Expanded))
    }

    val cardContentDescription = stringResource(R.string.reader_mode_panel_media_player_content_description)
    ListenSheet(
        article = uiState.article,
        articleProgressState = articleProgressState,
        playbackPhase = uiState.playbackPhase,
        speed = uiState.speed,
        voiceState = uiState.voiceState,
        onAction = onAction,
        onExpandClicked = { onAction(ListenAction.ModeChanged(PlayerMode.Expanded)) },
        expanded = mode == PlayerMode.Expanded,
        error = uiState.error,
        modifier =
            Modifier.fillMaxWidth()
                .semantics(
                    properties = {
                        contentDescription = cardContentDescription
                        isTraversalGroup = true
                    }
                ),
    )
}
