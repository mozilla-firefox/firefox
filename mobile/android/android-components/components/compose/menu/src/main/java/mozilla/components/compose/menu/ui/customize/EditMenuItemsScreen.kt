/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.compose.menu.ui.customize

import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.tooling.preview.PreviewLightDark
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

    CustomizeMenuCenteredList(modifier = modifier) { leftPadding, rightPadding ->
        LazyColumn(
            modifier = Modifier.fillMaxSize(),
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

            items(items = menuItems.shownRows, key = { it.key }) { row ->
                EditMenuItemRow(row = row, onExpansionToggle = { store.dispatch(ExpansionToggled(it)) })
            }

            item(key = HIDDEN_TITLE) {
                CustomizeMenuHiddenTitle(text = stringResource(R.string.mozac_menu_customize_hidden_items))
            }

            if (menuItems.hiddenRows.isEmpty()) {
                item(key = DROP_AREA) {
                    CustomizeMenuDropArea(
                        text = stringResource(R.string.mozac_menu_customize_drag_items_to_hide),
                        isDropTarget = false,
                    )
                }
            }

            items(items = menuItems.hiddenRows, key = { it.key }) { row ->
                EditMenuItemRow(row = row, onExpansionToggle = { store.dispatch(ExpansionToggled(it)) })
            }
        }
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
