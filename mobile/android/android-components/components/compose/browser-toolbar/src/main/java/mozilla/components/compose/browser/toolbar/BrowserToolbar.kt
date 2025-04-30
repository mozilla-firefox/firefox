/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.compose.browser.toolbar

import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import mozilla.components.browser.state.helper.Target
import mozilla.components.browser.state.state.SessionState
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarStore
import mozilla.components.lib.state.ext.observeAsComposableState
import mozilla.components.lib.state.ext.observeAsState

/**
 * A customizable toolbar for browsers.
 *
 * The toolbar can switch between two modes: display and edit. The display mode displays the current
 * URL and controls for navigation. In edit mode the current URL can be edited. Those two modes are
 * implemented by the [BrowserDisplayToolbar] and [BrowserEditToolbar] composables.
 *
 * @param store The [BrowserToolbarStore] to observe the UI state from.
 * @param browserStore The [BrowserStore] to observe the [target] from.
 * @param target The target tab to observe.
 * @param onTextEdit Function to get executed whenever the user edits the text in the toolbar in
 * "edit" mode.
 * @param onTextCommit Function to get executed when the user has finished editing the URL and wants
 * to load the entered text.
 * @param colors The color scheme the browser toolbar will use for the UI.
 */
@Composable
fun BrowserToolbar(
    store: BrowserToolbarStore,
    browserStore: BrowserStore,
    target: Target,
    onTextEdit: (String) -> Unit,
    onTextCommit: (String) -> Unit,
    colors: BrowserToolbarColors = BrowserToolbarDefaults.colors(),
) {
    val uiState by store.observeAsState(initialValue = store.state) { it }
    val selectedTab: SessionState? by target.observeAsComposableStateFrom(
        store = browserStore,
        observe = { tab -> tab?.content?.url },
    )
    val progressBarConfig = store.observeAsComposableState { it.displayState.progressBarConfig }.value

    val url = selectedTab?.content?.url ?: ""
    val input = when (val editText = uiState.editState.editText) {
        null -> url
        else -> editText
    }

    if (uiState.isEditMode()) {
        BrowserEditToolbar(
            url = input,
            colors = colors.editToolbarColors,
            editActionsStart = uiState.editState.editActionsStart,
            editActionsEnd = uiState.editState.editActionsEnd,
            onUrlCommitted = { text -> onTextCommit(text) },
            onUrlEdit = { text -> onTextEdit(text) },
            onInteraction = { store.dispatch(it) },
        )
    } else {
        BrowserDisplayToolbar(
            pageOrigin = uiState.displayState.pageOrigin,
            colors = colors.displayToolbarColors,
            progressBarConfig = progressBarConfig,
            browserActionsStart = uiState.displayState.browserActionsStart,
            pageActionsStart = uiState.displayState.pageActionsStart,
            pageActionsEnd = uiState.displayState.pageActionsEnd,
            browserActionsEnd = uiState.displayState.browserActionsEnd,
            onInteraction = { store.dispatch(it) },
        )
    }
}
