/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.debugsettings.listentopage

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateListOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.tooling.preview.PreviewLightDark
import kotlinx.coroutines.flow.distinctUntilChangedBy
import kotlinx.coroutines.flow.map
import mozilla.components.browser.state.selector.selectedTab
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.compose.base.button.FilledButton
import mozilla.components.feature.listentopage.ArticleProgress
import mozilla.components.feature.listentopage.ListenAction
import mozilla.components.feature.listentopage.ListenState
import mozilla.components.feature.listentopage.ListenStore
import mozilla.components.feature.listentopage.PlaybackState
import mozilla.components.feature.listentopage.listenReducer
import org.mozilla.fenix.components.listentopage.ListenDebugController
import org.mozilla.fenix.theme.FirefoxTheme

private const val MILLIS_PER_SECOND = 1000

/**
 * Tools for listen to page.
 *
 * @param listenStore The store a session is driven through and reported by.
 * @param browserStore Used to find the tab the "Listen" button reads out.
 * @param debugController Holds the failures forced on a tab from these tools.
 */
@Composable
fun ListenToPageTools(listenStore: ListenStore, browserStore: BrowserStore, debugController: ListenDebugController) {
    var showingLog by remember { mutableStateOf(false) }

    // Recorded out here rather than inside the log, which is only composed while it is on screen. The article's
    // length moves most in the opening seconds, so a session started from the button below is caught from its
    // first chunk.
    val lengthChanges = remember { mutableStateListOf<LengthChange>() }

    LaunchedEffect(listenStore) {
        listenStore.stateFlow
            .map { it.articleProgress }
            // Only when the length moves. The position moves every time the player is sampled, and logging those
            // would bury the length changes this is here to show.
            .distinctUntilChangedBy { it.durationMs }
            .collect { lengthChanges.add(LengthChange.of(previous = lengthChanges.lastOrNull(), current = it)) }
    }

    ListenToPageToolsContent(
        showingLog = showingLog,
        lengthChanges = lengthChanges,
        listenStore = listenStore,
        browserStore = browserStore,
        debugController = debugController,
        onChangeLogVisibility = { showingLog = it },
    )
}

@Composable
private fun ListenToPageToolsContent(
    showingLog: Boolean,
    lengthChanges: List<LengthChange>,
    listenStore: ListenStore,
    browserStore: BrowserStore,
    debugController: ListenDebugController,
    onChangeLogVisibility: (Boolean) -> Unit,
) {
    val listenState by listenStore.stateFlow.collectAsState()

    Surface {
        if (showingLog) {
            LengthLog(changes = lengthChanges, onBackClick = { onChangeLogVisibility(false) })
        } else {
            Column(
                modifier =
                    Modifier.padding(all = FirefoxTheme.layout.space.static200).verticalScroll(rememberScrollState()),
                verticalArrangement = Arrangement.spacedBy(FirefoxTheme.layout.space.static100),
            ) {
                Text(text = listenState.playbackState.describe())
                Text(text = listenState.articleProgress.describe())

                FilledButton(text = "Listen") {
                    browserStore.state.selectedTab?.let {
                        listenStore.dispatch(ListenAction.Session.ListenRequested(tabId = it.id, url = it.content.url))
                    }
                }

                FilledButton(text = "Cause extraction to fail for tab") {
                    browserStore.state.selectedTab?.let { debugController.failExtractionFor(it.id) }
                }

                FilledButton(text = "Timeline log") { onChangeLogVisibility(true) }
            }
        }
    }
}

@Composable
private fun LengthLog(changes: List<LengthChange>, onBackClick: () -> Unit) {
    Column(
        modifier = Modifier.padding(all = FirefoxTheme.layout.space.static200).verticalScroll(rememberScrollState()),
        verticalArrangement = Arrangement.spacedBy(FirefoxTheme.layout.space.static100),
    ) {
        FilledButton(text = "Back", onClick = onBackClick)

        if (changes.isEmpty()) {
            Text(text = "The article has no length yet. Press Listen to start a session.")
            return@Column
        }

        // Newest first, because the change worth reading is the one that just happened.
        changes.asReversed().forEachIndexed { fromEnd, change ->
            Text(text = "#${changes.size - fromEnd}  ${change.progress.describe()}")
            Text(text = "  length ${change.delta}", style = FirefoxTheme.typography.caption)

            HorizontalDivider()
        }
    }
}

/**
 * One report where the article's length differed from the report before it.
 *
 * The length only moves when a chunk has been made and measured, so an entry is an estimate being replaced by the real
 * thing, and [delta] is how far out the estimate was.
 *
 * @property progress The progress as it was reported.
 * @property delta How much the length moved by.
 */
private data class LengthChange(val progress: ArticleProgress, val delta: String) {
    companion object {
        fun of(previous: LengthChange?, current: ArticleProgress): LengthChange {
            val was = previous?.progress?.durationMs

            return LengthChange(
                progress = current,
                delta =
                    if (was == null) {
                        "${current.durationMs.asSeconds()} (first)"
                    } else {
                        (current.durationMs - was).asDelta()
                    },
            )
        }
    }
}

private fun PlaybackState.describe(): String {
    val duration = chunk.durationMs?.let { "${it / MILLIS_PER_SECOND}s" } ?: "unknown"

    return "${phase.name}, chunk ${chunk.index} at ${positionMs / MILLIS_PER_SECOND}s of $duration"
}

private fun ArticleProgress.describe(): String =
    if (durationMs <= 0) {
        "article has no length yet"
    } else {
        "${positionMs.asSeconds()} of ${durationMs.asSeconds()} (${(fraction * TO_PERCENT).toInt()}%)"
    }

private fun Long.asSeconds(): String = "%.1fs".format(this.toDouble() / MILLIS_PER_SECOND)

private fun Long.asDelta(): String = if (this > 0) "+${asSeconds()}" else asSeconds()

private const val TO_PERCENT = 100

@PreviewLightDark
@Composable
private fun PreviewListenToPageTools() {
    FirefoxTheme {
        ListenToPageTools(
            listenStore = ListenStore(initialState = ListenState(), ::listenReducer),
            browserStore = BrowserStore(),
            debugController = ListenDebugController(),
        )
    }
}
