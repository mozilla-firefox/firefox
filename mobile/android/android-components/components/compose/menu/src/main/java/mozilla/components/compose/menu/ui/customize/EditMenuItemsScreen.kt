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
import androidx.compose.ui.semantics.CollectionInfo
import androidx.compose.ui.semantics.collectionInfo
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.tooling.preview.PreviewLightDark
import androidx.compose.ui.unit.Dp
import mozilla.components.compose.base.theme.AcornTheme
import mozilla.components.compose.draggable.InteractionState
import mozilla.components.compose.draggable.ListInteractionState
import mozilla.components.compose.menu.R
import mozilla.components.compose.menu.store.CustomizableMenuItemsAction.AttemptedEmptySection
import mozilla.components.compose.menu.store.CustomizableMenuItemsAction.ExpansionToggled
import mozilla.components.compose.menu.store.CustomizableMenuItemsAction.ItemsChanged
import mozilla.components.compose.menu.store.CustomizableMenuItemsAction.PreviewClicked
import mozilla.components.compose.menu.store.CustomizableMenuItemsState
import mozilla.components.compose.menu.store.CustomizableMenuItemsStore
import mozilla.components.compose.menu.ui.customize.CustomizeMenuItemKey.DRAG_BAR
import mozilla.components.compose.menu.ui.customize.CustomizeMenuItemKey.DROP_AREA
import mozilla.components.compose.menu.ui.customize.CustomizeMenuItemKey.HIDDEN_TITLE
import mozilla.components.compose.menu.ui.utils.A11yUpdatesAnnouncer
import mozilla.components.compose.menu.ui.utils.editMenuItemsHiddenPreviewData
import mozilla.components.compose.menu.ui.utils.editMenuItemsPreviewData
import mozilla.components.compose.menu.ui.utils.rememberA11yUpdatesAnnouncer
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
            onAttemptedHide = { (it as? String)?.let { id -> store.dispatch(AttemptedEmptySection(id)) } },
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
    val announcer = rememberA11yUpdatesAnnouncer()
    val actions = rememberEditMenuItemsActions(store, menuItems, dragState, announcer)

    LazyColumn(
        modifier =
            Modifier.fillMaxSize()
                .onGloballyPositioned(dragState.interactionState::updateListLayoutCoordinates)
                .semantics { collectionInfo = CollectionInfo(rowCount = -1, columnCount = 1) },
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

        editMenuItemRows(menuItems.shownRows, firstIndex = 1, dragState, itemSpacing, actions)

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

        editMenuItemRows(menuItems.hiddenRows, dragState.firstHiddenIndex, dragState, itemSpacing, actions)
    }

    // Drawn over the list, important to be the last element here.
    announcer.LiveRegion()
}

@Composable
private fun rememberEditMenuItemsActions(
    store: CustomizableMenuItemsStore,
    menuItems: CustomizeMenuItems,
    dragState: CustomizeMenuDragState<CustomizeMenuItems>,
    announcer: A11yUpdatesAnnouncer,
): EditMenuItemsActions {
    val listState = dragState.listState
    val interactionState = dragState.interactionState
    return remember(store, menuItems, announcer, listState, interactionState) {
        fun canChange(): Boolean {
            val current = store.state
            val isStale =
                current.items != menuItems.shown ||
                    current.hiddenItems != menuItems.hidden ||
                    current.expandedItemIds != menuItems.expandedIds
            return !interactionState.isDragging() && !isStale
        }

        EditMenuItemsActions(
            items = menuItems,
            onExpansionToggle = { itemId ->
                if (!interactionState.isDragging()) store.dispatch(ExpansionToggled(itemId))
            },
            onMove = { itemId, moved, announcement ->
                if (canChange()) {
                    moved.listIndexOf(itemId)?.let { listState.moveAndKeepVisible(itemId, it) }
                    store.dispatch(ItemsChanged(items = moved.shown, hiddenItems = moved.hidden))
                    announcer.announce(announcement)
                }
            },
            onRejected = { itemId -> if (canChange()) store.dispatch(AttemptedEmptySection(itemId)) },
            announce = { announcement -> if (!interactionState.isDragging()) announcer.announce(announcement) },
        )
    }
}

private fun ListInteractionState.isDragging() = draggedItem != InteractionState.List.None

private fun LazyListScope.editMenuItemRows(
    rows: List<EditMenuItemsRow>,
    firstIndex: Int,
    dragState: CustomizeMenuDragState<CustomizeMenuItems>,
    itemSpacing: Dp,
    actions: EditMenuItemsActions,
) {
    itemsIndexed(items = rows, key = { _, row -> row.key }) { index, row ->
        EditMenuItemRow(
            row = row,
            position = firstIndex + index,
            dragState = dragState,
            itemSpacing = itemSpacing,
            actions = actions,
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
