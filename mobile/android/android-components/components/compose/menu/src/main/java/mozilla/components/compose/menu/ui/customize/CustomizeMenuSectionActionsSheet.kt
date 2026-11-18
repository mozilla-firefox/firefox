/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.compose.menu.ui.customize

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
import androidx.compose.material3.Surface
import androidx.compose.material3.rememberModalBottomSheetState
import androidx.compose.runtime.Composable
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
 * Bottom sheet with the [actions] moving a section of the customize menu screen, the ones which cannot move it now
 * shown disabled.
 *
 * @param actions The actions.
 * @param onAction Called with the moved groups and announcement of the chosen action, before the sheet hides.
 * @param onDismissRequest Called when the sheet is hidden, to stop showing it.
 */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
internal fun CustomizeMenuSectionActionsSheet(
    actions: List<CustomizeMenuSectionAction>,
    onAction: (movedGroups: CustomizeMenuGroups, announcement: String) -> Unit,
    onDismissRequest: () -> Unit,
) {
    val sheetState = rememberModalBottomSheetState(skipPartiallyExpanded = true)
    val scope = rememberCoroutineScope()
    // The actions as when one was chosen, for the sheet not to change while it hides.
    var chosenActions by remember { mutableStateOf<List<CustomizeMenuSectionAction>?>(null) }
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
        CustomizeMenuSectionActionsList(
            actions = chosenActions ?: actions,
            onAction = { movedGroups, announcement ->
                if (chosenActions == null) {
                    chosenActions = actions
                    onAction(movedGroups, announcement)
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
private fun CustomizeMenuSectionActionsList(
    actions: List<CustomizeMenuSectionAction>,
    onAction: (movedGroups: CustomizeMenuGroups, announcement: String) -> Unit,
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
                onInteraction = { action.movedGroups?.let { onAction(it, action.announcement) } },
                contentDescription = null,
                icon = MenuItemIconRes(action.icon),
                state = if (action.movedGroups == null) MenuItemState.DISABLED else MenuItemState.DEFAULT,
            )
        }
    }
}

/** Marks the click of an action. Only for the item to be clickable, it is not dispatched to any store. */
private data object ActionClicked : MenuEvent

@PreviewLightDark
@Composable
private fun CustomizeMenuSectionActionsListPreview() {
    AcornTheme {
        Surface(color = MaterialTheme.colorScheme.surface) {
            CustomizeMenuSectionActionsList(
                actions =
                    listOf(
                        CustomizeMenuSectionAction("Move up", iconsR.drawable.mozac_ic_chevron_up_24, null, ""),
                        CustomizeMenuSectionAction(
                            "Move down",
                            iconsR.drawable.mozac_ic_chevron_down_24,
                            CustomizeMenuGroups(emptyList(), emptyList()),
                            "",
                        ),
                        CustomizeMenuSectionAction(
                            "Hide section",
                            iconsR.drawable.mozac_ic_eye_slash_24,
                            CustomizeMenuGroups(emptyList(), emptyList()),
                            "",
                        ),
                    ),
                onAction = { _, _ -> },
            )
        }
    }
}
