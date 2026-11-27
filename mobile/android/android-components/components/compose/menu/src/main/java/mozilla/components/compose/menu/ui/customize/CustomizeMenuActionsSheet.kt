/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

@file:Suppress("MatchingDeclarationName")

package mozilla.components.compose.menu.ui.customize

import androidx.annotation.DrawableRes
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.BottomSheetDefaults
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.ModalBottomSheet
import androidx.compose.material3.SheetState
import androidx.compose.material3.SheetValue
import androidx.compose.material3.Surface
import androidx.compose.material3.rememberBottomSheetState
import androidx.compose.runtime.Composable
import androidx.compose.runtime.SideEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.dropShadow
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Shape
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.tooling.preview.PreviewLightDark
import androidx.compose.ui.unit.dp
import kotlinx.coroutines.launch
import mozilla.components.compose.base.BottomSheetHandle
import mozilla.components.compose.base.R as baseR
import mozilla.components.compose.base.text.Text
import mozilla.components.compose.base.theme.AcornTheme
import mozilla.components.compose.menu.data.MenuItemTitle
import mozilla.components.compose.menu.store.MenuEvent
import mozilla.components.compose.menu.ui.MenuItemIconRes
import mozilla.components.compose.menu.ui.MenuItemState
import mozilla.components.compose.menu.ui.MenuListItem
import mozilla.components.compose.menu.ui.getListItemShape
import mozilla.components.compose.menu.ui.menuListGroupSemantics
import mozilla.components.ui.icons.R as iconsR

/**
 * An action offered in the sheet opened by a long press on the drag handle of a section or an item of the customize
 * menu screens.
 *
 * @property label What the action is called.
 * @property icon Icon of the action in the sheet.
 * @property result What the action leads to, or `null` if it cannot be done now.
 * @property announcement What to tell the user once the action was done, unused if it cannot be done.
 */
internal data class CustomizeMenuAction<T : Any>(
    val label: String,
    @DrawableRes val icon: Int,
    val result: T?,
    val announcement: String,
)

/**
 * Bottom sheet with the [actions] of a section or an item of the customize menu screens, the ones which cannot be done
 * now shown disabled.
 *
 * @param actions The actions, or `null` when what they act on is gone: the sheet is then dismissed, unless an action
 *   was chosen, in which case it keeps showing the chosen actions while it hides.
 * @param onAction Called with the result and announcement of the chosen action, before the sheet hides.
 * @param onDismissRequest Called when the sheet is hidden, to stop showing it.
 */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
internal fun <T : Any> CustomizeMenuActionsSheet(
    actions: List<CustomizeMenuAction<T>>?,
    onAction: (result: T, announcement: String) -> Unit,
    onDismissRequest: () -> Unit,
) {
    val sheetState =
        rememberBottomSheetState(
            initialValue = SheetValue.Hidden,
            enabledValues = setOf(SheetValue.Hidden, SheetValue.Expanded),
        )
    val scope = rememberCoroutineScope()
    // The actions as when one was chosen, for the sheet not to change while it hides.
    var chosenActions by remember { mutableStateOf<List<CustomizeMenuAction<T>>?>(null) }
    val displayed = chosenActions ?: actions
    if (displayed == null) {
        SideEffect { onDismissRequest() }
        return
    }
    fun hideThenDismiss(onInterrupted: () -> Unit = {}) {
        scope
            .launch { sheetState.hide() }
            .invokeOnCompletion { if (sheetState.isVisible) onInterrupted() else onDismissRequest() }
    }

    ModalBottomSheet(
        onDismissRequest = onDismissRequest,
        modifier = Modifier.sheetShadow(sheetState, BottomSheetDefaults.ExpandedShape),
        sheetState = sheetState,
        containerColor = MaterialTheme.colorScheme.surface,
        scrimColor = Color.Transparent,
        dragHandle = {
            BottomSheetHandle(
                onRequestDismiss = { hideThenDismiss() },
                contentDescription = stringResource(baseR.string.mozac_compose_base_close_button_content_description),
                modifier = Modifier.padding(AcornTheme.layout.space.static200),
            )
        },
    ) {
        CustomizeMenuActionsList(
            actions = displayed,
            onAction = { result, announcement ->
                if (chosenActions == null) {
                    chosenActions = displayed
                    onAction(result, announcement)
                    hideThenDismiss(onInterrupted = { chosenActions = null })
                }
            },
        )
    }
}

/**
 * Shadow of the sheet, which [ModalBottomSheet] has none of. The sheet is placed by its offset inside the bounds this
 * is applied to, so the shadow is moved by it too.
 */
@OptIn(ExperimentalMaterial3Api::class)
private fun Modifier.sheetShadow(sheetState: SheetState, shape: Shape): Modifier =
    this.dropShadow(shape) {
            offset = Offset(0f, sheetState.requireOffset() + 8.dp.toPx())
            radius = 12.dp.toPx()
            spread = 6.dp.toPx()
            color = Color.Black.copy(alpha = 0.15f)
        }
        .dropShadow(shape) {
            offset = Offset(0f, sheetState.requireOffset() + 4.dp.toPx())
            radius = 4.dp.toPx()
            color = Color.Black.copy(alpha = 0.3f)
        }

@Composable
private fun <T : Any> CustomizeMenuActionsList(
    actions: List<CustomizeMenuAction<T>>,
    onAction: (result: T, announcement: String) -> Unit,
) {
    Column(
        modifier =
            Modifier.verticalScroll(rememberScrollState())
                .padding(
                    start = AcornTheme.layout.space.static200,
                    end = AcornTheme.layout.space.static200,
                    bottom = AcornTheme.layout.space.static200,
                )
                .clip(MaterialTheme.shapes.extraLarge)
                .menuListGroupSemantics(actions.size),
        verticalArrangement = Arrangement.spacedBy(AcornTheme.layout.space.static25),
    ) {
        actions.forEachIndexed { index, action ->
            MenuListItem(
                title = MenuItemTitle(Text.String(action.label)),
                modifier = Modifier.clip(getListItemShape(index, actions.size)),
                index = index,
                onClickEvent = ActionClicked,
                onInteraction = { action.result?.let { onAction(it, action.announcement) } },
                contentDescription = null,
                icon = MenuItemIconRes(action.icon),
                state = if (action.result == null) MenuItemState.DISABLED else MenuItemState.DEFAULT,
            )
        }
    }
}

/** Marks the click of an action. Only for the item to be clickable, it is not dispatched to any store. */
private data object ActionClicked : MenuEvent

@PreviewLightDark
@Composable
private fun CustomizeMenuActionsListPreview() {
    AcornTheme {
        Surface(color = MaterialTheme.colorScheme.surface) {
            CustomizeMenuActionsList(
                actions =
                    listOf(
                        CustomizeMenuAction<Unit>("Move up", iconsR.drawable.mozac_ic_chevron_up_24, null, ""),
                        CustomizeMenuAction("Move down", iconsR.drawable.mozac_ic_chevron_down_24, Unit, ""),
                        CustomizeMenuAction("Hide section", iconsR.drawable.mozac_ic_eye_slash_24, Unit, ""),
                    ),
                onAction = { _, _ -> },
            )
        }
    }
}
