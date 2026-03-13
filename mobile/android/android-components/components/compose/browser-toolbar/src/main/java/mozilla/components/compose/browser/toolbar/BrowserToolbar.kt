/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.compose.browser.toolbar

import androidx.compose.material3.DividerDefaults
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.toArgb
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.tooling.preview.PreviewLightDark
import androidx.compose.ui.unit.dp
import mozilla.components.compose.base.theme.AcornTheme
import mozilla.components.compose.browser.toolbar.concept.PageOrigin
import mozilla.components.compose.browser.toolbar.store.BrowserEditToolbarAction.SearchQueryUpdated
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarAction.CommitUrl
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarInteraction.BrowserToolbarEvent
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarState
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarStore
import mozilla.components.compose.browser.toolbar.store.DisplayState
import mozilla.components.compose.browser.toolbar.store.EditState
import mozilla.components.compose.browser.toolbar.store.Mode
import mozilla.components.compose.browser.toolbar.store.ToolbarGravity
import mozilla.components.compose.browser.toolbar.ui.BrowserToolbarQuery
import mozilla.components.compose.cfr.CFRPopup
import mozilla.components.compose.cfr.CFRPopupLayout
import mozilla.components.compose.cfr.CFRPopupProperties
import mozilla.components.lib.state.ext.observeAsComposableState

private const val CFR_HORIZONTAL_OFFSET = 160
private const val CFR_VERTICAL_OFFSET = 0

/**
 * Represents the state and behavior of a CFR shown in the browser toolbar.
 *
 * @property enabled Whether the CFR is currently active and should be displayed.
 * @property title The headline text displayed in the CFR banner.
 * @property description A short descriptive message explaining the feature or action.
 * @property onShown Callback invoked when the CFR is first shown to the user.
 * @property onDismiss Callback invoked when the CFR is dismissed.
 */
data class BrowserToolbarCFR(
    val enabled: Boolean,
    val title: String,
    val description: String,
    val onShown: () -> Unit = {},
    val onDismiss: (Boolean) -> Unit = {},
)

/**
 * A customizable toolbar for browsers.
 *
 * The toolbar can switch between two modes: display and edit. The display mode displays the current
 * URL and controls for navigation. In edit mode the current URL can be edited. Those two modes are
 * implemented by the [BrowserDisplayToolbar] and [BrowserEditToolbar] composables.
 *
 * @param store The [BrowserToolbarStore] to observe the UI state from.
 * @param cfr The [BrowserToolbarCFR] to hold properties of Toolbar's CFR.
 * @property useMinimalBottomToolbarWhenEnteringText Whether to show a smaller height addressbar
 * with just the URL when using a bottom toolbar and the user is entering text in a website.
 */
@Composable
fun BrowserToolbar(
    store: BrowserToolbarStore,
    cfr: BrowserToolbarCFR? = null,
    useMinimalBottomToolbarWhenEnteringText: Boolean = false,
    backgroundColor: Color = MaterialTheme.colorScheme.surface,
    outlineColor: Color = DividerDefaults.color,
) {
    val uiState by store.observeAsComposableState { it }
    val cfrProperties = browserToolbarCFRProperties(uiState.gravity)

    if (uiState.isEditMode()) {
        BrowserEditToolbar(
            query = uiState.editState.query.current,
            hint = stringResource(uiState.editState.hint),
            isQueryPrefilled = uiState.editState.isQueryPrefilled,
            usePrivateModeQueries = uiState.editState.isQueryPrivate,
            gravity = ToolbarGravity.Top,
            backgroundColor = AcornTheme.colors.layer3,
            outlineColor = AcornTheme.colors.layer3,
            suggestion = uiState.editState.suggestion,
            editActionsStart = uiState.editState.editActionsStart,
            editActionsEnd = uiState.editState.editActionsEnd,
            onUrlCommitted = { text -> store.dispatch(CommitUrl(text)) },
            onUrlEdit = { store.dispatch(SearchQueryUpdated(it)) },
            onInteraction = { store.dispatch(it) },
        )
    } else {
        val displayToolbar = @Composable {
            BrowserDisplayToolbar(
                pageOrigin = uiState.displayState.pageOrigin,
                progressBarConfig = uiState.displayState.progressBarConfig,
                gravity = ToolbarGravity.Top,
                backgroundColor = AcornTheme.colors.layer3,
                outlineColor = AcornTheme.colors.layer3,
                browserActionsStart = uiState.displayState.browserActionsStart,
                pageActionsStart = uiState.displayState.pageActionsStart,
                pageActionsEnd = uiState.displayState.pageActionsEnd,
                browserActionsEnd = uiState.displayState.browserActionsEnd,
                onInteraction = { store.dispatch(it) },
                useMinimalBottomToolbarWhenEnteringText = useMinimalBottomToolbarWhenEnteringText,
            )
        }

        if (cfr?.enabled == true) {
            // Wrapping the toolbar with the CFR code negatively impacts the transition
            // between the full and minimal toolbar <=> Avoid this when not needed.
            CFRPopupLayout(
                showCFR = true,
                properties = cfrProperties,
                onCFRShown = { cfr.onShown.invoke() },
                onDismiss = { explicit -> cfr.onDismiss.invoke(explicit) },
                title = {
                    cfr.title.let {
                        Text(
                            text = it,
                            color = AcornTheme.colors.textOnColorPrimary,
                            style = AcornTheme.typography.subtitle2,
                        )
                    }
                },
                text = {
                    cfr.description.let {
                        Text(
                            text = it,
                            color = AcornTheme.colors.textOnColorPrimary,
                            style = AcornTheme.typography.body2,
                        )
                    }
                },
            ) {
                displayToolbar()
            }
        } else {
            displayToolbar()
        }
    }
}

@Composable
private fun browserToolbarCFRProperties(
    gravity: ToolbarGravity,
): CFRPopupProperties {
    val isBottom = gravity == ToolbarGravity.Bottom
    val indicatorDir =
        if (isBottom) CFRPopup.IndicatorDirection.DOWN else CFRPopup.IndicatorDirection.UP

    val colors = AcornTheme.colors

    return remember(isBottom) {
        CFRPopupProperties(
            popupAlignment = CFRPopup.PopupAlignment.INDICATOR_CENTERED_IN_ANCHOR,
            popupBodyColors = listOf(
                colors.layerGradientEnd.toArgb(),
                colors.layerGradientStart.toArgb(),
            ),
            dismissButtonColor = colors.iconOnColor.toArgb(),
            indicatorDirection = indicatorDir,
            popupVerticalOffset = CFR_VERTICAL_OFFSET.dp,
            indicatorArrowStartOffset = CFR_HORIZONTAL_OFFSET.dp,
        )
    }
}

@PreviewLightDark
@Composable
private fun BrowserToolbarPreview_EditMode() {
    // Mock edit state
    val editState = EditState(
        query = BrowserToolbarQuery("https://www.mozilla.org"),
        suggestion = null,
        editActionsStart = emptyList(),
        editActionsEnd = emptyList(),
    )
    val toolbarState = BrowserToolbarState(
        mode = Mode.EDIT,
        editState = editState,
    )
    val store = BrowserToolbarStore(toolbarState)

    AcornTheme {
        BrowserToolbar(
            store = store,
        )
    }
}

@PreviewLightDark
@Composable
private fun BrowserToolbarPreview_DisplayMode() {
    val mockPageOrigin = PageOrigin(
        hint = 0,
        title = "Preview Title",
        url = "https://www.mozilla.org",
        onClick = object : BrowserToolbarEvent {},
        onLongClick = null,
        textGravity = PageOrigin.Companion.TextGravity.TEXT_GRAVITY_START,
    )
    val displayState = DisplayState(
        pageOrigin = mockPageOrigin,
        browserActionsStart = emptyList(),
        pageActionsStart = emptyList(),
        pageActionsEnd = emptyList(),
        browserActionsEnd = emptyList(),
    )
    val toolbarState = BrowserToolbarState(
        mode = Mode.DISPLAY,
        displayState = displayState,
    )
    val store = BrowserToolbarStore(toolbarState)

    AcornTheme {
        BrowserToolbar(
            store = store,
        )
    }
}
