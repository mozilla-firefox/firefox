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
import androidx.compose.runtime.State
import androidx.compose.runtime.derivedStateOf
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.alpha
import androidx.compose.ui.layout.layout
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
 * @param isHidden Whether these groups were removed from the menu.
 */
internal fun LazyListScope.customizeMenuGroups(
    menuGroups: List<MenuItemsGroup>,
    dragState: CustomizeMenuDragState,
    firstIndex: Int,
    itemSpacing: Dp,
    isHidden: Boolean = false,
) {
    val interactionState = dragState.interactionState
    itemsIndexed(items = menuGroups, key = { _, group -> group.id }) { index, group ->
        val isDragged by
            remember(interactionState, group.id) {
                derivedStateOf { interactionState.draggedItem.key == group.id }
            }

        InteractableDragItemContainer(
            state = interactionState,
            key = group.id,
            position = firstIndex + index,
            modifier =
                Modifier.groupPlacement(
                    isDragged = isDragged,
                    dropTarget = dragState.dropTarget,
                    itemSpacing = itemSpacing,
                ),
        ) {
            CustomizeMenuGroupContent(
                group = group,
                modifier =
                    Modifier.padding(top = itemSpacing)
                        .dropLine(group.id, dragState.dropTarget, itemSpacing)
                        .alpha(if (isDragged) DRAGGED_GROUP_ALPHA else 1f)
                        .padding(
                            start = AcornTheme.layout.space.static200,
                            end = AcornTheme.layout.space.static25,
                        ),
                state = dragState.groups.stateOf(group, isDragged = isDragged, isHidden = isHidden),
                dragState = dragState,
            )
        }
    }
}

/**
 * Lay out a group without taking any space while it is dragged, for the other groups to take its space, showing there
 * if it is dropped back.
 *
 * @param isDragged Whether the group is dragged.
 * @param dropTarget Where the dragged group is dropped.
 * @param itemSpacing Space above each item of the list, which the dragged group takes too.
 */
@Composable
private fun Modifier.groupPlacement(
    isDragged: Boolean,
    dropTarget: State<CustomizeMenuDropTarget?>,
    itemSpacing: Dp,
): Modifier = this.then(if (isDragged) Modifier.collapsed().inPlaceDropLine(dropTarget, itemSpacing) else Modifier)

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

/** Lay out without taking any space, drawing over what is laid out next. */
private fun Modifier.collapsed() = layout { measurable, constraints ->
    val placeable = measurable.measure(constraints)
    layout(placeable.width, 0) { placeable.place(0, 0) }
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
                    menuGroups = dragState.groups.shown,
                    dragState = dragState,
                    firstIndex = 0,
                    itemSpacing = 0.dp,
                )
            }
        }
    }
}
