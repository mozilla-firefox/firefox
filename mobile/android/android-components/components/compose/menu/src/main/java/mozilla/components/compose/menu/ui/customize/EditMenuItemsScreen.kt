/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.compose.menu.ui.customize

import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.LazyListScope
import androidx.compose.foundation.lazy.itemsIndexed
import androidx.compose.foundation.lazy.rememberLazyListState
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.layout.onGloballyPositioned
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.tooling.preview.PreviewLightDark
import androidx.compose.ui.unit.Dp
import mozilla.components.compose.base.theme.AcornTheme
import mozilla.components.compose.menu.R
import mozilla.components.compose.menu.store.CustomizableMenuItemsAction.ExpansionToggled
import mozilla.components.compose.menu.store.CustomizableMenuItemsAction.PreviewClicked
import mozilla.components.compose.menu.store.CustomizableMenuItemsState
import mozilla.components.compose.menu.store.CustomizableMenuItemsStore
import mozilla.components.compose.menu.ui.customize.CustomizeMenuItemKey.DRAG_BAR
import mozilla.components.compose.menu.ui.customize.CustomizeMenuItemKey.DROP_AREA
import mozilla.components.compose.menu.ui.customize.CustomizeMenuItemKey.HIDDEN_TITLE
import mozilla.components.compose.menu.ui.utils.editMenuItemsHiddenPreviewData
import mozilla.components.compose.menu.ui.utils.editMenuItemsPreviewData
import mozilla.components.lib.state.ext.observeAsComposableState

/**
 * Content composable for the screen customizing the items of a menu section.
 *
 * @param store The [CustomizableMenuItemsStore] managing the state and dispatching events for the items customization.
 * @param modifier The [Modifier] to be applied to the layout.
 */
@Composable
fun EditMenuItemsContent(
    store: CustomizableMenuItemsStore,
    modifier: Modifier = Modifier,
) {
    val state by store.observeAsComposableState { it }
    val menuItems =
        remember(state.items, state.hiddenItems, state.expandedItemIds) {
            CustomizeMenuItems(state.items, state.hiddenItems, state.expandedItemIds)
        }
    val listState = rememberLazyListState()
    val dragState =
        rememberHideableListDragState(
            items = menuItems,
            listState = listState,
            itemInteractionHandler = remember(store, menuItems) { MoveItemsHandler(store, menuItems) },
            fixedItems = FIXED_ITEMS,
            onAttemptedHide = {},
        )

    CustomizeMenuCenteredList(modifier = modifier) { leftPadding, rightPadding ->
        EditMenuItemsList(
            store = store,
            menuItems = menuItems,
            dragState = dragState,
            leftPadding = leftPadding,
            rightPadding = rightPadding,
        )
    }
}

@Composable
private fun EditMenuItemsList(
    store: CustomizableMenuItemsStore,
    menuItems: CustomizeMenuItems,
    dragState: CustomizeMenuDragState<CustomizeMenuItems>,
    leftPadding: Dp,
    rightPadding: Dp,
) {
    val itemSpacing = AcornTheme.layout.space.static50
    val onExpansionToggle = { itemId: String -> store.dispatch(ExpansionToggled(itemId)) }

    LazyColumn(
        modifier = Modifier.fillMaxSize().onGloballyPositioned(dragState.interactionState::updateListLayoutCoordinates),
        state = dragState.listState,
        userScrollEnabled = !dragState.isDragging,
        contentPadding =
            PaddingValues.Absolute(
                left = leftPadding,
                right = rightPadding,
                bottom = AcornTheme.layout.space.static300,
            ),
        horizontalAlignment = Alignment.CenterHorizontally,
    ) {
        item(key = DRAG_BAR) {
            CustomizeMenuDragBar(
                text = stringResource(R.string.mozac_menu_customize_drag_items_to_reorder),
                onPreviewClick = { store.dispatch(PreviewClicked) },
            )
        }

        editMenuItemRows(menuItems.shownRows, firstIndex = 1, dragState, itemSpacing, onExpansionToggle)

        item(key = HIDDEN_TITLE) {
            HiddenSectionsItem(
                title = stringResource(R.string.mozac_menu_customize_hidden_items),
                dragState = dragState,
                itemSpacing = itemSpacing,
            )
        }

        if (!dragState.hasHiddenItems) {
            item(key = DROP_AREA) {
                DropAreaItem(
                    text = stringResource(R.string.mozac_menu_customize_drag_items_to_hide),
                    dragState = dragState,
                )
            }
        }

        editMenuItemRows(menuItems.hiddenRows, dragState.firstHiddenIndex, dragState, itemSpacing, onExpansionToggle)
    }
}

private fun LazyListScope.editMenuItemRows(
    rows: List<EditMenuItemsRow>,
    firstIndex: Int,
    dragState: CustomizeMenuDragState<CustomizeMenuItems>,
    itemSpacing: Dp,
    onExpansionToggle: (itemId: String) -> Unit,
) {
    itemsIndexed(items = rows, key = { _, row -> row.key }) { index, row ->
        EditMenuItemRow(
            row = row,
            position = firstIndex + index,
            dragState = dragState,
            itemSpacing = itemSpacing,
            onExpansionToggle = onExpansionToggle,
        )
    }
}

@PreviewLightDark
@Composable
private fun EditMenuItemsContentPreview() {
    AcornTheme {
        val store = remember {
            CustomizableMenuItemsStore(
                CustomizableMenuItemsState(
                    items = editMenuItemsPreviewData,
                    hiddenItems = editMenuItemsHiddenPreviewData,
                    expandedItemIds = setOf("more"),
                )
            )
        }
        EditMenuItemsContent(store)
    }
}
