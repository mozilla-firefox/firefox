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
import androidx.compose.ui.layout.onGloballyPositioned
import androidx.compose.ui.tooling.preview.PreviewLightDark
import mozilla.components.compose.base.text.Text
import mozilla.components.compose.base.theme.AcornTheme
import mozilla.components.compose.draggable.InteractableDragItemContainer
import mozilla.components.compose.menu.data.MenuItemTitle
import mozilla.components.compose.menu.data.MenuItemsGroup
import mozilla.components.compose.menu.data.StandardMenuItem
import mozilla.components.compose.menu.store.CustomizableMenuState
import mozilla.components.compose.menu.store.CustomizableMenuStore
import mozilla.components.compose.menu.ui.MenuItemIconRes
import mozilla.components.ui.icons.R as iconsR

private const val DRAGGED_GROUP_ALPHA = 0.7f

/**
 * Show [menuGroups] as items of a customize menu screen.
 *
 * @param menuGroups The groups of menu items to display.
 * @param drag State of dragging the groups of the screen.
 * @param firstIndex Index in the list of the first of these groups.
 * @param isHidden Whether these groups were removed from the menu.
 */
internal fun LazyListScope.customizeMenuGroups(
    menuGroups: List<MenuItemsGroup>,
    drag: CustomizeMenuDragState,
    firstIndex: Int,
    isHidden: Boolean = false,
) {
    val interactionState = drag.interactionState
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
                    isLastHidden = isHidden && index == menuGroups.lastIndex,
                    hiddenArea = drag.hiddenArea,
                ),
        ) {
            CustomizeMenuGroupContent(
                group = group,
                modifier =
                    Modifier.alpha(if (isDragged) DRAGGED_GROUP_ALPHA else 1f)
                        .padding(
                            start = AcornTheme.layout.space.static200,
                            end = AcornTheme.layout.space.static25,
                        ),
                state = drag.groups.stateOf(group, isDragged = isDragged, isHidden = isHidden),
            )
        }
    }
}

/**
 * Tell [hiddenArea] where the last hidden group is placed.
 *
 * @param isLastHidden Whether the group is the last of the hidden groups, at the bottom of the list.
 * @param hiddenArea Where the area holding the hidden groups is shown.
 */
private fun Modifier.groupPlacement(isLastHidden: Boolean, hiddenArea: CustomizeMenuHiddenArea): Modifier =
    this.then(if (isLastHidden) Modifier.onGloballyPositioned(hiddenArea::onLastHiddenGroupPositioned) else Modifier)

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
            val drag = rememberCustomizeMenuDragState(store, store.state, listState)
            LazyColumn(state = listState) {
                customizeMenuGroups(menuGroups = drag.groups.shown, drag = drag, firstIndex = 0)
            }
        }
    }
}
