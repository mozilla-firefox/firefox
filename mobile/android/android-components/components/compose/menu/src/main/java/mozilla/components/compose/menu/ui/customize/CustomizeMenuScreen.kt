/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.compose.menu.ui.customize

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.LazyItemScope
import androidx.compose.foundation.lazy.LazyListState
import androidx.compose.foundation.lazy.rememberLazyListState
import androidx.compose.material3.MaterialTheme
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
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.tooling.preview.PreviewLightDark
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import mozilla.components.compose.base.button.OutlinedButton
import mozilla.components.compose.base.theme.AcornTheme
import mozilla.components.compose.menu.R
import mozilla.components.compose.menu.store.CustomizableMenuAction.EditGroupItemsClicked
import mozilla.components.compose.menu.store.CustomizableMenuAction.PreviewClicked
import mozilla.components.compose.menu.store.CustomizableMenuAction.ResetToDefaultConfirmed
import mozilla.components.compose.menu.store.CustomizableMenuState
import mozilla.components.compose.menu.store.CustomizableMenuStore
import mozilla.components.compose.menu.ui.customize.CustomizeMenuItemKey.DRAG_BAR
import mozilla.components.compose.menu.ui.customize.CustomizeMenuItemKey.DROP_AREA
import mozilla.components.compose.menu.ui.customize.CustomizeMenuItemKey.HIDDEN_TITLE
import mozilla.components.compose.menu.ui.customize.CustomizeMenuItemKey.RESET_BUTTON
import mozilla.components.compose.menu.ui.utils.HIDDEN_SECTIONS_TITLE_PART
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
    var isResetDialogShown by rememberSaveable { mutableStateOf(false) }
    CustomizeMenuCenteredList(modifier = modifier) { leftPadding, rightPadding ->
        CustomizeMenuList(
            store = store,
            leftPadding = leftPadding,
            rightPadding = rightPadding,
            onResetToDefaultClick = { isResetDialogShown = true },
        )
    }

    if (isResetDialogShown) {
        ResetToDefaultDialog(
            onConfirm = {
                isResetDialogShown = false
                store.dispatch(ResetToDefaultConfirmed)
            },
            onDismiss = { isResetDialogShown = false },
        )
    }
}

/**
 * Layout for the lists of the customize menu screens, taking the whole width to be scrolled from anywhere, with the
 * items no wider than the maximum width and centered in the window, as far as the free width allows.
 *
 * @param modifier The [Modifier] to be applied to the layout.
 * @param content The list, given the left and right padding to apply to its content.
 */
@Composable
internal fun CustomizeMenuCenteredList(
    modifier: Modifier = Modifier,
    content: @Composable (leftPadding: Dp, rightPadding: Dp) -> Unit,
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
        content(freeWidth / 2 - shift, freeWidth / 2 + shift)
    }
}

@Suppress("LongMethod")
@Composable
private fun CustomizeMenuList(
    store: CustomizableMenuStore,
    leftPadding: Dp,
    rightPadding: Dp,
    onResetToDefaultClick: () -> Unit,
) {
    val menuState by store.observeAsComposableState { it }
    val listState = rememberLazyListState()
    val dragState = rememberCustomizeMenuDragState(store, menuState, listState)
    val itemSpacing = AcornTheme.layout.space.static150
    val announcer = rememberA11yUpdatesAnnouncer()
    var actionsGroupId by rememberSaveable { mutableStateOf<String?>(null) }
    val onMove = { movedGroupId: String, movedGroups: CustomizeMenuGroups, moveAnnouncement: String ->
        movedGroups.listIndexOf(movedGroupId)?.let { listState.moveAndKeepVisible(movedGroupId, it) }
        store.saveMove(dragState.items, movedGroupId, movedGroups)
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
        item(key = DRAG_BAR) { DragBarItem(onPreviewClick = { store.dispatch(PreviewClicked) }) }

        customizeMenuGroups(
            menuGroups = dragState.items.shown,
            dragState = dragState,
            firstIndex = 1,
            itemSpacing = itemSpacing,
            onMove = onMove,
            highlightedGroupId = actionsGroupId,
            onShowActions = { actionsGroupId = it },
            editAction = { id ->
                { store.dispatch(EditGroupItemsClicked(id)) }.takeIf { id in menuState.editableGroupIds }
            },
        )

        item(key = HIDDEN_TITLE) {
            HiddenSectionsItem(
                title = stringResource(R.string.mozac_menu_customize_hidden_sections),
                dragState = dragState,
                itemSpacing = itemSpacing,
            )
        }

        if (!dragState.hasHiddenItems) {
            item(key = DROP_AREA) {
                DropAreaItem(text = stringResource(R.string.mozac_menu_customize_drag_to_hide), dragState = dragState)
            }
        }

        customizeMenuGroups(
            menuGroups = dragState.items.hidden,
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
                    onClick = onResetToDefaultClick,
                )
            }
        }
    }

    CustomizeMenuSectionActionsSheetOf(
        groupId = actionsGroupId,
        groups = dragState.items,
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
    CustomizeMenuActionsSheet(
        actions = customizeMenuSectionActions(group, groups),
        onAction = { movedGroups, announcement -> onMove(group.id, movedGroups, announcement) },
        onDismissRequest = onDismissRequest,
    )
}

/**
 * Keep the item with [key] where it is in view, or bring it into view, once it moves to [index] in the list, for it to
 * stay laid out and keep the focus of accessibility services.
 */
internal fun LazyListState.moveAndKeepVisible(key: Any, index: Int) {
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

/**
 * The [DRAG_BAR] item of the list.
 *
 * @param onPreviewClick Called when the "Preview" button is clicked.
 */
@Composable
private fun DragBarItem(onPreviewClick: () -> Unit) {
    CustomizeMenuDragBar(
        text = stringResource(R.string.mozac_menu_customize_drag_to_reorder),
        onPreviewClick = onPreviewClick,
    )
}

/**
 * The [HIDDEN_TITLE] item of the list: the space keeping the hidden groups or items in place while a shown one is
 * dragged, then their title, each a node of the item of its own (see [HiddenAreaSpace]).
 *
 * @param title The title shown above the hidden groups or items.
 * @param dragState State of dragging the items of the screen.
 * @param itemSpacing Space between the items of the list.
 */
@Composable
internal fun LazyItemScope.HiddenSectionsItem(title: String, dragState: CustomizeMenuDragState<*>, itemSpacing: Dp) {
    HiddenAreaSpace(
        dragState = dragState,
        itemSpacing = itemSpacing,
        modifier = Modifier.drawnOnlyInView(HIDDEN_TITLE, dragState).animateItem(),
    )
    CustomizeMenuHiddenTitle(
        text = title,
        modifier =
            Modifier.drawnOnlyInView(HIDDEN_TITLE, dragState)
                .animateItem()
                .dropLineSpace(
                    key = HIDDEN_TITLE,
                    isDragged = false,
                    dropTarget = dragState.dropTarget,
                    extraSpace = dragState.extraSpace,
                    itemSpacing = itemSpacing,
                    part = HIDDEN_SECTIONS_TITLE_PART,
                    sides = setOf(DropLineSpace.BELOW),
                )
                .padding(top = itemSpacing),
    )
}

/**
 * The [DROP_AREA] item of the list, where shown groups or items are dropped to hide them while none is hidden.
 *
 * @param text What to tell the user to drag into it.
 * @param dragState State of dragging the items of the screen.
 */
@Composable
internal fun LazyItemScope.DropAreaItem(text: String, dragState: CustomizeMenuDragState<*>) {
    val isDropTarget by
        remember(dragState.dropTarget) {
            derivedStateOf { dragState.dropTarget.value == CustomizeMenuDropTarget.EmptyHiddenArea }
        }
    CustomizeMenuDropArea(
        text = text,
        isDropTarget = isDropTarget,
        modifier = Modifier.drawnOnlyInView(DROP_AREA, dragState).animateItem(),
    )
}

/**
 * The [RESET_BUTTON] item of the list.
 *
 * @param dragState State of dragging the items of the screen.
 * @param itemSpacing Space between the items of the list.
 * @param onClick Called when the button is clicked.
 */
@Composable
private fun LazyItemScope.ResetButtonItem(dragState: CustomizeMenuDragState<*>, itemSpacing: Dp, onClick: () -> Unit) {
    CustomizeMenuResetButton(
        onClick = onClick,
        modifier = Modifier.drawnOnlyInView(RESET_BUTTON, dragState).animateItem().padding(top = itemSpacing),
    )
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
