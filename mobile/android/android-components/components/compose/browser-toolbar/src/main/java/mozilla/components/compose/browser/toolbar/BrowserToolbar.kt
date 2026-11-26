/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.compose.browser.toolbar

import androidx.compose.material3.DividerDefaults
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.tooling.preview.PreviewLightDark
import mozilla.components.compose.base.cfr.CFR
import mozilla.components.compose.base.cfr.CFRTooltip
import mozilla.components.compose.base.cfr.IndicatorDirection
import mozilla.components.compose.base.cfr.rememberCFRPositionProvider
import mozilla.components.compose.base.cfr.rememberCFRState
import mozilla.components.compose.base.theme.AcornTheme
import mozilla.components.compose.browser.toolbar.concept.PageOrigin
import mozilla.components.compose.browser.toolbar.store.BrowserDisplayToolbarAction
import mozilla.components.compose.browser.toolbar.store.BrowserEditToolbarAction.SearchQueryUpdated
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarAction.CommitUrl
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarInteraction.BrowserToolbarEvent
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarState
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarStore
import mozilla.components.compose.browser.toolbar.store.DisplayState
import mozilla.components.compose.browser.toolbar.store.EditState
import mozilla.components.compose.browser.toolbar.store.Mode
import mozilla.components.compose.browser.toolbar.store.ToolbarGravity.Bottom
import mozilla.components.compose.browser.toolbar.store.ToolbarGravity.Top
import mozilla.components.compose.browser.toolbar.ui.BrowserToolbarQuery
import mozilla.components.lib.state.ext.observeAsComposableState

/**
 * A customizable toolbar for browsers.
 *
 * The toolbar can switch between two modes: display and edit. The display mode displays the current URL and controls
 * for navigation. In edit mode the current URL can be edited. Those two modes are implemented by the
 * [BrowserDisplayToolbar] and [BrowserEditToolbar] composables.
 *
 * @param store The [BrowserToolbarStore] to observe the UI state from.
 * @param cfr The [BrowserToolbarCFR] to hold properties of Toolbar's CFR.
 * @property useMinimalBottomToolbarWhenEnteringText Whether to show a smaller height addressbar with just the URL when
 *   using a bottom toolbar and the user is entering text in a website.
 * @param browserActionsColor Optional `onSurface` color override applied only to the display toolbar's browser actions
 *   (outside the URL bounding box), leaving page actions unchanged.
 * @param showDivider Whether to show the divider on the edge of the toolbar facing the webpage.
 */
@Composable
fun BrowserToolbar(
    store: BrowserToolbarStore,
    cfr: BrowserToolbarCFR? = null,
    useMinimalBottomToolbarWhenEnteringText: Boolean = false,
    browserActionsColor: Color? = null,
    showDivider: Boolean = true,
) {
    val uiState by store.observeAsComposableState { it }
    val backgroundColor = MaterialTheme.colorScheme.surface
    val outlineColor = DividerDefaults.color

    if (uiState.isEditMode()) {
        BrowserEditToolbar(
            query = uiState.editState.query.current,
            hint = stringResource(uiState.editState.hint),
            isQueryPrefilled = uiState.editState.isQueryPrefilled,
            usePrivateModeQueries = uiState.editState.isQueryPrivate,
            gravity = uiState.gravity,
            backgroundColor =
                if (store.state.editState.query.current.isEmpty()) {
                    backgroundColor
                } else {
                    MaterialTheme.colorScheme.surface
                },
            outlineColor = outlineColor,
            showDivider = showDivider,
            suggestion = uiState.editState.suggestion,
            editActionsStart = uiState.editState.editActionsStart,
            editActionsEnd = uiState.editState.editActionsEnd,
            onUrlCommitted = { text -> store.dispatch(CommitUrl(text)) },
            onUrlEdit = { store.dispatch(SearchQueryUpdated(it)) },
            onInteraction = { store.dispatch(it) },
        )
    } else {
        val displayToolbar =
            @Composable {
                BrowserDisplayToolbar(
                    pageOrigin = uiState.displayState.pageOrigin,
                    progressBarConfig = uiState.displayState.progressBarConfig,
                    gravity = uiState.gravity,
                    backgroundColor = backgroundColor,
                    outlineColor = outlineColor,
                    browserActionsStart = uiState.displayState.browserActionsStart,
                    pageActionsStart = uiState.displayState.pageActionsStart,
                    pageActionsEnd = uiState.displayState.pageActionsEnd,
                    browserActionsEnd = uiState.displayState.browserActionsEnd,
                    browserActionsColor = browserActionsColor,
                    onInteraction = { store.dispatch(it) },
                    useMinimalBottomToolbarWhenEnteringText = useMinimalBottomToolbarWhenEnteringText,
                    showDivider = showDivider,
                )
            }

        if (cfr?.enabled == true) {
            // Wrapping the toolbar with the CFR code negatively impacts the transition
            // between the full and minimal toolbar <=> Avoid this when not needed.
            DisplayToolbarWithCFR(
                cfr = cfr,
                indicatorDirection =
                    when (uiState.gravity) {
                        Top -> IndicatorDirection.UP
                        Bottom -> IndicatorDirection.DOWN
                    },
                onCFRDismissed = {
                    store.dispatch(BrowserDisplayToolbarAction.ToolbarCFRDismissed(tag = cfr.tag))
                },
                modifier = Modifier,
            ) {
                displayToolbar()
            }
        } else {
            displayToolbar()
        }
    }
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
private fun DisplayToolbarWithCFR(
    cfr: BrowserToolbarCFR,
    indicatorDirection: IndicatorDirection,
    onCFRDismissed: (tag: String) -> Unit,
    modifier: Modifier = Modifier,
    displayToolbar: @Composable () -> Unit,
) {
    val title: @Composable (() -> Unit)? =
        cfr.title?.run {
            { Text(text = stringResource(cfr.title)) }
        }

    val state = rememberCFRState()
    val onDismiss = {
        state.dismiss()
        onCFRDismissed(cfr.tag)
    }

    CFRTooltip(
        modifier = modifier,
        state = state,
        positionProvider = rememberCFRPositionProvider(indicatorDirection),
        cfr = {
            CFR(
                modifier = Modifier,
                showDismissButton = true,
                onDismiss = onDismiss,
                title = title,
                text = {
                    Text(text = stringResource(cfr.description))
                },
            )
        },
        onDismissRequest = {
            onDismiss()
        },
    ) {
        displayToolbar()
    }

    LaunchedEffect(cfr) {
        if (cfr.enabled) {
            state.show()
        }
    }
}

@PreviewLightDark
@Composable
private fun BrowserToolbarPreview_EditMode() {
    // Mock edit state
    val editState =
        EditState(
            query = BrowserToolbarQuery("https://www.mozilla.org"),
            suggestion = null,
            editActionsStart = emptyList(),
            editActionsEnd = emptyList(),
        )
    val toolbarState =
        BrowserToolbarState(
            mode = Mode.EDIT,
            editState = editState,
        )
    val store = BrowserToolbarStore(toolbarState)

    AcornTheme {
        BrowserToolbar(store = store)
    }
}

@PreviewLightDark
@Composable
private fun BrowserToolbarPreview_DisplayMode() {
    val mockPageOrigin =
        PageOrigin(
            hint = 0,
            title = "Preview Title",
            url = "https://www.mozilla.org",
            onClick = object : BrowserToolbarEvent {},
            onLongClick = null,
            textGravity = PageOrigin.Companion.TextGravity.TEXT_GRAVITY_START,
        )
    val displayState =
        DisplayState(
            pageOrigin = mockPageOrigin,
            browserActionsStart = emptyList(),
            pageActionsStart = emptyList(),
            pageActionsEnd = emptyList(),
            browserActionsEnd = emptyList(),
        )
    val toolbarState =
        BrowserToolbarState(
            mode = Mode.DISPLAY,
            displayState = displayState,
        )
    val store = BrowserToolbarStore(toolbarState)

    AcornTheme {
        BrowserToolbar(store = store)
    }
}
