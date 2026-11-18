/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.compose.menu.ui.customize

import androidx.compose.foundation.background
import androidx.compose.foundation.basicMarquee
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.defaultMinSize
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.wrapContentHeight
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.LazyItemScope
import androidx.compose.foundation.lazy.LazyListState
import androidx.compose.foundation.lazy.rememberLazyListState
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.derivedStateOf
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.layout.onGloballyPositioned
import androidx.compose.ui.layout.positionInWindow
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.platform.LocalWindowInfo
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.CollectionInfo
import androidx.compose.ui.semantics.collectionInfo
import androidx.compose.ui.semantics.heading
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.tooling.preview.PreviewLightDark
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import mozilla.components.compose.base.button.OutlinedButton
import mozilla.components.compose.base.button.TextButton
import mozilla.components.compose.base.theme.AcornTheme
import mozilla.components.compose.menu.R
import mozilla.components.compose.menu.store.CustomizableMenuAction.PreviewClicked
import mozilla.components.compose.menu.store.CustomizableMenuAction.ResetToDefaultClicked
import mozilla.components.compose.menu.store.CustomizableMenuState
import mozilla.components.compose.menu.store.CustomizableMenuStore
import mozilla.components.compose.menu.ui.customize.CustomizeMenuItemKey.DRAG_BAR
import mozilla.components.compose.menu.ui.customize.CustomizeMenuItemKey.DROP_AREA
import mozilla.components.compose.menu.ui.customize.CustomizeMenuItemKey.HIDDEN_SECTIONS
import mozilla.components.compose.menu.ui.customize.CustomizeMenuItemKey.RESET_BUTTON
import mozilla.components.compose.menu.ui.utils.customizeMenuPreviewData
import mozilla.components.compose.menu.ui.utils.ext.saveMove
import mozilla.components.compose.menu.ui.utils.rememberA11yUpdatesAnnouncer
import mozilla.components.lib.state.ext.observeAsComposableState

/**
 * Content composable for the customize menu screen.
 *
 * @param store The [CustomizableMenuStore] managing the state and dispatching events for menu customization.
 * @param modifier The [Modifier] to be applied to the layout.
 */
@Composable
fun CustomizeMenuContent(
    store: CustomizableMenuStore,
    modifier: Modifier = Modifier,
) {
    val windowWidth = LocalWindowInfo.current.containerSize.width
    // How far right of the center of the window this is, when beside a larger notch than navigation bar for example.
    var offCenter by remember { mutableFloatStateOf(0f) }
    // The list takes the whole width, to be scrolled from anywhere, with its items no wider than the maximum width.
    BoxWithConstraints(
        modifier =
            modifier.fillMaxSize().background(MaterialTheme.colorScheme.surfaceContainer).onGloballyPositioned {
                offCenter = it.positionInWindow().x + (it.size.width - windowWidth) / 2f
            }
    ) {
        val freeWidth =
            if (constraints.hasBoundedWidth) {
                (maxWidth - AcornTheme.layout.size.containerMaxWidth).coerceAtLeast(0.dp)
            } else {
                0.dp
            }
        // The items are centered in the window, as far as the free width allows.
        val shift = with(LocalDensity.current) { offCenter.toDp() }.coerceIn(-freeWidth / 2, freeWidth / 2)
        CustomizeMenuList(
            store = store,
            leftPadding = freeWidth / 2 - shift,
            rightPadding = freeWidth / 2 + shift,
        )
    }
}

@Composable
private fun CustomizeMenuList(
    store: CustomizableMenuStore,
    leftPadding: Dp,
    rightPadding: Dp,
) {
    val menuState by store.observeAsComposableState { it }
    val listState = rememberLazyListState()
    val dragState = rememberCustomizeMenuDragState(store, menuState, listState)
    val itemSpacing = AcornTheme.layout.space.static150
    val announcer = rememberA11yUpdatesAnnouncer()
    var actionsGroupId by rememberSaveable { mutableStateOf<String?>(null) }
    val onMove = { movedGroupId: String, movedGroups: CustomizeMenuGroups, moveAnnouncement: String ->
        movedGroups.listIndexOf(movedGroupId)?.let { listState.moveAndKeepVisible(movedGroupId, it) }
        store.saveMove(dragState.groups, movedGroupId, movedGroups)
        announcer.announce(moveAnnouncement)
    }

    LazyColumn(
        modifier =
            Modifier.fillMaxSize()
                .onGloballyPositioned(dragState.interactionState::updateListLayoutCoordinates)
                // Not all items are menu sections so avoid having them all counted and read to the user.
                // Each section tells separately of its position among the shown ones.
                .semantics { collectionInfo = CollectionInfo(rowCount = -1, columnCount = 1) },
        state = listState,
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
            CustomizeMenuDragBar({ store.dispatch(PreviewClicked) })
        }

        customizeMenuGroups(
            menuGroups = dragState.groups.shown,
            dragState = dragState,
            firstIndex = 1,
            itemSpacing = itemSpacing,
            onMove = onMove,
            highlightedGroupId = actionsGroupId,
            onShowActions = { actionsGroupId = it },
        )

        item(key = HIDDEN_SECTIONS) {
            HiddenSectionsItem(dragState = dragState, itemSpacing = itemSpacing)
        }

        if (!dragState.hasHiddenGroups) {
            item(key = DROP_AREA) { DropAreaItem(dragState = dragState) }
        }

        customizeMenuGroups(
            menuGroups = dragState.groups.hidden,
            dragState = dragState,
            firstIndex = dragState.firstHiddenIndex,
            itemSpacing = itemSpacing,
            onMove = onMove,
            highlightedGroupId = actionsGroupId,
            onShowActions = { actionsGroupId = it },
            isHidden = true,
        )

        if (menuState.isCustomized) {
            item(key = RESET_BUTTON) {
                ResetButtonItem(
                    dragState = dragState,
                    itemSpacing = itemSpacing,
                    onClick = { store.dispatch(ResetToDefaultClicked) },
                )
            }
        }
    }

    CustomizeMenuSectionActionsSheetOf(
        groupId = actionsGroupId,
        groups = dragState.groups,
        onMove = onMove,
        onDismissRequest = { actionsGroupId = null },
    )

    // Drawn over the list, important to be the last element here.
    // Will allow for accessibility services to be told of its updates, which they aren't if it is covered.
    announcer.LiveRegion()
}

/**
 * Bottom sheet with the actions moving the group with [groupId], shown only while [groups] has it.
 *
 * @param groupId Id of the group, if any.
 * @param groups The shown and the hidden groups.
 * @param onMove Called with the id of the group, the moved groups and the announcement of the chosen action.
 * @param onDismissRequest Called when the sheet is hidden, to stop showing it.
 */
@Composable
private fun CustomizeMenuSectionActionsSheetOf(
    groupId: String?,
    groups: CustomizeMenuGroups,
    onMove: (movedGroupId: String, movedGroups: CustomizeMenuGroups, announcement: String) -> Unit,
    onDismissRequest: () -> Unit,
) {
    val group = (groups.shown + groups.hidden).firstOrNull { it.id == groupId } ?: return
    CustomizeMenuSectionActionsSheet(
        actions = customizeMenuSectionActions(group, groups),
        onAction = { movedGroups, announcement -> onMove(group.id, movedGroups, announcement) },
        onDismissRequest = onDismissRequest,
    )
}

/**
 * Keep the item with [key] where it is in view, or bring it into view, once it moves to [index] in the list, for it to
 * stay laid out and keep the focus of accessibility services.
 */
private fun LazyListState.moveAndKeepVisible(key: Any, index: Int) {
    val layout = layoutInfo
    val item = layout.visibleItemsInfo.firstOrNull { it.key == key }
    if (item == null) {
        requestScrollToItem(index)
        return
    }
    val lastFullyVisibleOffset = (layout.viewportEndOffset - item.size).coerceAtLeast(layout.viewportStartOffset)
    val offset = item.offset.coerceIn(layout.viewportStartOffset, lastFullyVisibleOffset)
    requestScrollToItem(index, scrollOffset = -offset)
}

@Composable
private fun CustomizeMenuResetButton(
    onClick: () -> Unit,
    modifier: Modifier = Modifier,
) {
    Box(
        modifier =
            modifier
                .fillMaxWidth()
                .padding(
                    start = AcornTheme.layout.space.static200,
                    top = AcornTheme.layout.space.static300 + AcornTheme.layout.space.static150,
                    end = AcornTheme.layout.space.static200,
                    bottom = 0.dp,
                ),
        contentAlignment = Alignment.Center,
    ) {
        OutlinedButton(
            text = stringResource(R.string.mozac_menu_customize_reset_to_default),
            onClick = onClick,
        )
    }
}

@Composable
private fun CustomizeMenuDragBar(
    onPreviewClick: () -> Unit,
    modifier: Modifier = Modifier,
) {
    Row(
        modifier =
            modifier
                .fillMaxWidth()
                .defaultMinSize(minHeight = AcornTheme.layout.space.static600)
                .background(MaterialTheme.colorScheme.surfaceContainer)
                .padding(
                    start = AcornTheme.layout.space.static200,
                    end = AcornTheme.layout.space.static100,
                ),
        horizontalArrangement = Arrangement.SpaceBetween,
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Text(
            text = stringResource(R.string.mozac_menu_customize_drag_to_reorder),
            modifier = Modifier.weight(1f).basicMarquee().semantics { heading() },
            style = AcornTheme.typography.headline8,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
            maxLines = 1,
        )
        TextButton(
            text = stringResource(R.string.mozac_menu_customize_preview),
            onClick = onPreviewClick,
        )
    }
}

/**
 * The [HIDDEN_SECTIONS] item of the list: the space keeping the hidden sections in place while a shown group is
 * dragged, then their title, each a node of the item of its own (see [HiddenAreaSpace]).
 *
 * @param dragState State of dragging the groups of the screen.
 * @param itemSpacing Space between the items of the list.
 */
@Composable
private fun LazyItemScope.HiddenSectionsItem(dragState: CustomizeMenuDragState, itemSpacing: Dp) {
    HiddenAreaSpace(
        dragState = dragState,
        itemSpacing = itemSpacing,
        modifier = Modifier.drawnOnlyInView(HIDDEN_SECTIONS, dragState).animateItem(),
    )
    CustomizeMenuHiddenSectionsTitle(
        modifier =
            Modifier.drawnOnlyInView(HIDDEN_SECTIONS, dragState)
                .animateItem()
                .padding(top = itemSpacing)
                .dropLine(HIDDEN_SECTIONS, dragState.dropTarget, itemSpacing, placeAfter = true)
    )
}

/**
 * The [DROP_AREA] item of the list, where shown groups are dropped to hide them while none is hidden.
 *
 * @param dragState State of dragging the groups of the screen.
 */
@Composable
private fun LazyItemScope.DropAreaItem(dragState: CustomizeMenuDragState) {
    val isDropTarget by
        remember(dragState.dropTarget) {
            derivedStateOf { dragState.dropTarget.value == CustomizeMenuDropTarget.EmptyHiddenArea }
        }
    CustomizeMenuDropArea(
        isDropTarget = isDropTarget,
        modifier = Modifier.drawnOnlyInView(DROP_AREA, dragState).animateItem(),
    )
}

/**
 * The [RESET_BUTTON] item of the list.
 *
 * @param dragState State of dragging the groups of the screen.
 * @param itemSpacing Space between the items of the list.
 * @param onClick Called when the button is clicked.
 */
@Composable
private fun LazyItemScope.ResetButtonItem(dragState: CustomizeMenuDragState, itemSpacing: Dp, onClick: () -> Unit) {
    CustomizeMenuResetButton(
        onClick = onClick,
        modifier = Modifier.drawnOnlyInView(RESET_BUTTON, dragState).animateItem().padding(top = itemSpacing),
    )
}

@Composable
private fun CustomizeMenuHiddenSectionsTitle(modifier: Modifier = Modifier) {
    Text(
        text = stringResource(R.string.mozac_menu_customize_hidden_sections),
        modifier =
            modifier
                .semantics { heading() }
                .fillMaxWidth()
                .defaultMinSize(minHeight = AcornTheme.layout.space.static600)
                .padding(
                    start = AcornTheme.layout.space.static200,
                    end = AcornTheme.layout.space.static100,
                    top = AcornTheme.layout.space.static100,
                    bottom = AcornTheme.layout.space.static50,
                )
                .wrapContentHeight(Alignment.CenterVertically),
        style = AcornTheme.typography.headline8,
        color = MaterialTheme.colorScheme.onSurfaceVariant,
    )
}

/**
 * The empty area to drag groups into to hide them.
 *
 * @param isDropTarget Whether a dragged group is dropped into it, which highlights it.
 * @param modifier The [Modifier] to be applied to the area.
 */
@Composable
private fun CustomizeMenuDropArea(isDropTarget: Boolean, modifier: Modifier = Modifier) {
    Box(
        modifier =
            modifier
                .fillMaxWidth()
                .padding(
                    start = AcornTheme.layout.space.static200,
                    top = AcornTheme.layout.space.static150,
                    end = AcornTheme.layout.space.static200,
                )
                .then(
                    if (isDropTarget) {
                        Modifier.background(
                            color = MaterialTheme.colorScheme.tertiaryContainer,
                            shape = MaterialTheme.shapes.extraLarge,
                        )
                    } else {
                        Modifier
                    }
                )
                .hiddenSectionBorder(isHighlighted = isDropTarget)
                .defaultMinSize(minHeight = 72.dp),
        contentAlignment = Alignment.Center,
    ) {
        Text(
            text = stringResource(R.string.mozac_menu_customize_drag_to_hide),
            style = AcornTheme.typography.body2,
            color =
                if (isDropTarget) {
                    MaterialTheme.colorScheme.tertiary
                } else {
                    MaterialTheme.colorScheme.onSurfaceVariant
                },
        )
    }
}

@PreviewLightDark
@Composable
private fun CustomizeMenuScreenWithTabsShownPreview() {
    AcornTheme {
        CustomizeMenuContent(
            CustomizableMenuStore(
                initialState =
                    CustomizableMenuState(
                        menuGroups = customizeMenuPreviewData,
                        isCustomized = false,
                    )
            )
        )
    }
}

@PreviewLightDark
@Composable
private fun CustomizeMenuScreenWithTabsHiddenPreview() {
    AcornTheme {
        CustomizeMenuContent(
            CustomizableMenuStore(
                initialState =
                    CustomizableMenuState(
                        menuGroups = customizeMenuPreviewData,
                        hiddenGroupIds = setOf("tabs"),
                        isCustomized = true,
                    )
            )
        )
    }
}
