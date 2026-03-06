/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.compose.browser.toolbar

import androidx.appcompat.content.res.AppCompatResources
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.DividerDefaults
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalResources
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.semantics.testTagsAsResourceId
import androidx.compose.ui.tooling.preview.Preview
import androidx.compose.ui.tooling.preview.PreviewLightDark
import androidx.compose.ui.unit.dp
import mozilla.components.compose.base.theme.AcornTheme
import mozilla.components.compose.base.theme.acornPrivateColorScheme
import mozilla.components.compose.base.theme.privateColorPalette
import mozilla.components.compose.browser.toolbar.concept.Action
import mozilla.components.compose.browser.toolbar.concept.Action.ActionButtonRes
import mozilla.components.compose.browser.toolbar.concept.Action.SearchSelectorAction
import mozilla.components.compose.browser.toolbar.concept.Action.SearchSelectorAction.ContentDescription.StringResContentDescription
import mozilla.components.compose.browser.toolbar.concept.Action.SearchSelectorAction.Icon.DrawableIcon
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarInteraction.BrowserToolbarEvent
import mozilla.components.compose.browser.toolbar.store.ToolbarGravity
import mozilla.components.compose.browser.toolbar.store.ToolbarGravity.Bottom
import mozilla.components.compose.browser.toolbar.store.ToolbarGravity.Top
import mozilla.components.compose.browser.toolbar.ui.BrowserToolbarQuery
import mozilla.components.compose.browser.toolbar.ui.InlineAutocompleteTextField
import mozilla.components.concept.toolbar.AutocompleteResult
import mozilla.components.ui.icons.R as iconsR

private val ROUNDED_CORNER_SHAPE = RoundedCornerShape(90.dp)

/**
 * Sub-component of the [BrowserToolbar] responsible for allowing the user to edit the current
 * URL ("edit mode").
 *
 * @param query The current query.
 * @param hint Hint to show in the absence of a query.
 * @param suggestion [AutocompleteResult] to show as an inline autocomplete suggestion for the current [query].
 * @param isQueryPrefilled Whether [query] is prefilled and not user entered.
 * @param usePrivateModeQueries Whether queries should be done in private / incognito mode.
 * @param gravity [ToolbarGravity] for where the toolbar is being placed on the screen.
 * @param backgroundColor Color of the background.
 * @param outlineColor Color of the divider.
 * @param editActionsStart List of [Action]s to be displayed at the start of the URL of
 * the edit toolbar.
 * @param editActionsEnd List of [Action]s to be displayed at the end of the URL of
 * the edit toolbar.
 * @param onUrlEdit Will be called when the URL value changes. An updated text value comes as a
 * parameter of the callback.
 * @param onUrlCommitted Will be called when the user has finished editing and wants to initiate
 * loading the entered URL. The committed text value comes as a parameter of the callback.
 * @param onInteraction Callback for handling [BrowserToolbarEvent]s on user interactions.
 */
@Composable
fun BrowserEditToolbar(
    query: String,
    hint: String,
    suggestion: AutocompleteResult? = null,
    isQueryPrefilled: Boolean = false,
    usePrivateModeQueries: Boolean = false,
    gravity: ToolbarGravity = Top,
    backgroundColor: Color = MaterialTheme.colorScheme.surface,
    outlineColor: Color = DividerDefaults.color,
    editActionsStart: List<Action> = emptyList(),
    editActionsEnd: List<Action> = emptyList(),
    onUrlEdit: (BrowserToolbarQuery) -> Unit = {},
    onUrlCommitted: (String) -> Unit = {},
    onInteraction: (BrowserToolbarEvent) -> Unit,
) {
    Surface(color = backgroundColor) {
        Box(
            modifier = Modifier
                .fillMaxWidth()
                .semantics { testTagsAsResourceId = true },
        ) {
            Row(
                modifier = Modifier
                    .fillMaxWidth()
                    .padding(start = 12.dp, end = 12.dp, bottom =
                        LocalResources.current.getDimensionPixelSize(com.android.car.navioui.R.dimen.padding_M).dp)
                    .height(72.dp)
                    .clip(shape = ROUNDED_CORNER_SHAPE)
                    .background(color = AcornTheme.colors.controlsInputFocusFill),
                verticalAlignment = Alignment.CenterVertically,
            ) {
                ActionContainer(
                    modifier = Modifier
                        .padding(horizontal =
                            LocalResources.current.getDimensionPixelSize(
                                com.android.car.navioui.R.dimen.padding_M).dp),
                    actions = editActionsStart,
                    onInteraction = onInteraction,
                )

                InlineAutocompleteTextField(
                    query = query,
                    hint = hint,
                    suggestion = suggestion,
                    showQueryAsPreselected = isQueryPrefilled,
                    usePrivateModeQueries = usePrivateModeQueries,
                    modifier = Modifier.weight(1f),
                    onUrlEdit = onUrlEdit,
                    onUrlCommitted = onUrlCommitted,
                )

                ActionContainer(
                    actions = editActionsEnd,
                    onInteraction = onInteraction,
                )
            }

            HorizontalDivider(
                modifier = Modifier.align(
                    when (gravity) {
                        Top -> Alignment.BottomCenter
                        Bottom -> Alignment.TopCenter
                    },
                ),
                color = outlineColor,
            )
        }
    }
}

@PreviewLightDark
@Composable
private fun BrowserEditToolbarPreview() {
    AcornTheme {
        BrowserEditToolbar(
            query = "http://www.mozilla.org",
            hint = "Search or enter address",
            gravity = Top,
            suggestion = null,
            editActionsStart = listOf(
                SearchSelectorAction(
                    icon = DrawableIcon(
                        AppCompatResources.getDrawable(LocalContext.current, iconsR.drawable.mozac_ic_search_24)!!,
                    ),
                    contentDescription = StringResContentDescription(android.R.string.untitled),
                    menu = { emptyList() },
                    onClick = null,
                ),
            ),
            editActionsEnd = listOf(
                ActionButtonRes(
                    drawableResId = iconsR.drawable.mozac_ic_microphone_24,
                    contentDescription = android.R.string.untitled,
                    onClick = object : BrowserToolbarEvent {},
                ),
//                ActionButtonRes(
//                    drawableResId = iconsR.drawable.mozac_ic_qr_code_24,
//                    contentDescription = android.R.string.untitled,
//                    onClick = object : BrowserToolbarEvent {},
//                ),
            ),
            onInteraction = {},
        )
    }
}

@Preview
@Composable
private fun BrowserEditToolbarPrivatePreview() {
    AcornTheme(
        colors = privateColorPalette,
        colorScheme = acornPrivateColorScheme(),
    ) {
        BrowserEditToolbar(
            query = "http://www.mozilla.org",
            hint = "Search or enter address",
            gravity = Top,
            suggestion = null,
            editActionsStart = listOf(
                SearchSelectorAction(
                    icon = DrawableIcon(
                        AppCompatResources.getDrawable(LocalContext.current, iconsR.drawable.mozac_ic_search_24)!!,
                    ),
                    contentDescription = StringResContentDescription(android.R.string.untitled),
                    menu = { emptyList() },
                    onClick = null,
                ),
            ),
            editActionsEnd = listOf(

            ),
            onInteraction = {},
        )
    }
}
