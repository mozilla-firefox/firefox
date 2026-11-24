/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.compose.menu.ui.customize

import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.LazyListScope
import androidx.compose.foundation.lazy.itemsIndexed
import androidx.compose.foundation.lazy.rememberLazyListState
import androidx.compose.material3.Surface
import androidx.compose.runtime.Composable
import androidx.compose.runtime.derivedStateOf
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.alpha
import androidx.compose.ui.tooling.preview.PreviewLightDark
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import mozilla.components.compose.base.text.Text
import mozilla.components.compose.base.theme.AcornTheme
import mozilla.components.compose.draggable.InteractableDragItemContainer
import mozilla.components.compose.menu.data.MenuItemTitle
import mozilla.components.compose.menu.data.MenuItemsGroup
import mozilla.components.compose.menu.data.StandardMenuItem
import mozilla.components.compose.menu.store.CustomizableMenuState
import mozilla.components.compose.menu.store.CustomizableMenuStore
import mozilla.components.compose.menu.ui.MenuItemIconRes
import mozilla.components.compose.menu.ui.utils.DRAGGED_GROUP_ALPHA
import mozilla.components.ui.icons.R as iconsR

/**
 * Show [menuGroups] as items of a customize menu screen.
 *
 * @param menuGroups The groups of menu items to display.
 * @param dragState State of dragging the groups of the screen.
 * @param firstIndex Index in the list of the first of these groups.
 * @param itemSpacing Space between the items of the list. It is added above each group, not by the list, for a dragged
 *   group, which takes no space, to not leave more of it between the groups around it.
 * @param onMove Called when the custom action of an accessibility service moves a group, with its id and the groups as
 *   moved, to save them, and what to tell the user about the move.
 * @param highlightedGroupId Id of the group to highlight, as the one the open actions sheet acts on, if any.
 * @param onShowActions Called with the id of a group when the user long presses its drag handle, to show its actions.
 * @param editAction The action editing the items of the group with this id, or `null` when they can't be edited.
 * @param isHidden Whether these groups were removed from the menu.
 */
internal fun LazyListScope.customizeMenuGroups(
    menuGroups: List<MenuItemsGroup>,
    dragState: CustomizeMenuDragState<CustomizeMenuGroups>,
    firstIndex: Int,
    itemSpacing: Dp,
    onMove: (movedGroupId: String, movedGroups: CustomizeMenuGroups, announcement: String) -> Unit,
    highlightedGroupId: String?,
    onShowActions: (groupId: String) -> Unit,
    editAction: (groupId: String) -> (() -> Unit)? = { null },
    isHidden: Boolean = false,
) {
    val interactionState = dragState.interactionState
    itemsIndexed(items = menuGroups, key = { _, group -> group.id }) { index, group ->
        val isDragged by
            remember(interactionState, group.id) {
                derivedStateOf { interactionState.draggedItem.key == group.id }
            }

        val onEdit = editAction(group.id).takeIf { !isHidden && !isDragged }

        InteractableDragItemContainer(
            state = interactionState,
            key = group.id,
            position = firstIndex + index,
            modifier =
                Modifier.drawnOnlyInView(group.id, dragState)
                    .dragPlacement(
                        key = group.id,
                        isDragged = isDragged,
                        dragState = dragState,
                        itemSpacing = itemSpacing,
                    ),
        ) {
            CustomizeMenuGroupContent(
                group = group,
                modifier =
                    Modifier.customizeMenuGroupSemantics(
                            group = group,
                            groups = dragState.items,
                            onMove = { movedGroups, announcement -> onMove(group.id, movedGroups, announcement) },
                            onEdit = onEdit,
                        )
                        .padding(top = itemSpacing)
                        .alpha(if (isDragged) DRAGGED_GROUP_ALPHA else 1f)
                        .padding(
                            start = AcornTheme.layout.space.static200,
                            end = AcornTheme.layout.space.static25,
                        ),
                state = dragState.items.stateOf(group, isDragged = isDragged, isHidden = isHidden),
                dragState = dragState,
                isHighlighted = group.id == highlightedGroupId,
                onEditClick = onEdit,
                onDragHandleLongPress = { onShowActions(group.id) },
            )
        }
    }
}

/** How [group] is shown. Dragged groups look the same, whether they were shown or hidden. */
private fun CustomizeMenuGroups.stateOf(
    group: MenuItemsGroup,
    isDragged: Boolean,
    isHidden: Boolean,
): CustomizeMenuGroupState =
    when {
        isDragged -> CustomizeMenuGroupState.SHOWN
        isHidden -> CustomizeMenuGroupState.HIDDEN
        group.id in mandatoryIds -> CustomizeMenuGroupState.MANDATORY
        else -> CustomizeMenuGroupState.SHOWN
    }

@PreviewLightDark
@Composable
private fun CustomizeMenuPreview() {
    AcornTheme {
        Surface {
            val store = remember {
                CustomizableMenuStore(
                    CustomizableMenuState(
                        menuGroups =
                            listOf(
                                MenuItemsGroup.Row(
                                    id = "group1",
                                    items =
                                        listOf(
                                            StandardMenuItem(
                                                title = MenuItemTitle(Text.String("Bookmarks")),
                                                icon = MenuItemIconRes(iconsR.drawable.mozac_ic_bookmark_24),
                                            ),
                                            StandardMenuItem(
                                                title = MenuItemTitle(Text.String("History")),
                                                icon = MenuItemIconRes(iconsR.drawable.mozac_ic_history_24),
                                            ),
                                        ),
                                ),
                                MenuItemsGroup.Grid(
                                    id = "group2",
                                    items =
                                        listOf(
                                            StandardMenuItem(
                                                title = MenuItemTitle(Text.String("Settings")),
                                                icon = MenuItemIconRes(iconsR.drawable.mozac_ic_settings_24),
                                            ),
                                            StandardMenuItem(
                                                title = MenuItemTitle(Text.String("Downloads")),
                                                icon = MenuItemIconRes(iconsR.drawable.mozac_ic_download_24),
                                            ),
                                        ),
                                ),
                            )
                    )
                )
            }
            val listState = rememberLazyListState()
            val dragState = rememberCustomizeMenuDragState(store, store.state, listState)
            LazyColumn(state = listState) {
                customizeMenuGroups(
                    menuGroups = dragState.items.shown,
                    dragState = dragState,
                    firstIndex = 0,
                    itemSpacing = 0.dp,
                    onMove = { _, _, _ -> },
                    highlightedGroupId = null,
                    onShowActions = {},
                )
            }
        }
    }
}
