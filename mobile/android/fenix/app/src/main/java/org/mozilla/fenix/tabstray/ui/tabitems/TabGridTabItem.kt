/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.tabstray.ui.tabitems

import androidx.compose.foundation.Image
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.aspectRatio
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.wrapContentHeight
import androidx.compose.foundation.layout.wrapContentSize
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.SwipeToDismissBox
import androidx.compose.material3.SwipeToDismissBoxState
import androidx.compose.material3.SwipeToDismissBoxValue
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.CollectionItemInfo
import androidx.compose.ui.semantics.CustomAccessibilityAction
import androidx.compose.ui.semantics.collectionItemInfo
import androidx.compose.ui.semantics.customActions
import androidx.compose.ui.semantics.selected
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.semantics.testTag
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.tooling.preview.PreviewLightDark
import androidx.compose.ui.tooling.preview.PreviewParameter
import androidx.compose.ui.unit.dp
import mozilla.components.browser.state.state.TabSessionState
import mozilla.components.compose.base.button.IconButton
import mozilla.components.concept.engine.utils.ABOUT_HOME_URL
import mozilla.components.support.base.utils.MAX_URI_LENGTH
import mozilla.components.ui.icons.R as iconsR
import org.mozilla.fenix.R
import org.mozilla.fenix.compose.Favicon
import org.mozilla.fenix.compose.TabThumbnail
import org.mozilla.fenix.tabstray.TabsTrayTestTag
import org.mozilla.fenix.tabstray.browser.compose.TabItemInteractionState
import org.mozilla.fenix.tabstray.data.TabsTrayItem
import org.mozilla.fenix.tabstray.data.createTab
import org.mozilla.fenix.theme.FirefoxTheme
import org.mozilla.fenix.theme.ThemedValue
import org.mozilla.fenix.theme.ThemedValueProvider

private val TabHeaderFaviconSize = 12.dp

/**
 * Tab grid item used to display a tab that supports clicks, long clicks, multiple selection, and media controls.
 *
 * @param tab The given tab to render as a grid item.
 * @param swipeToDismissBoxState The swipe state of the item.
 * @param swipingEnabled Whether swipe-to-dismiss is enabled.
 * @param interactionState The tab item's interaction state (hover, drag, etc)
 * @param onCloseClick Invoked when the close button is clicked.
 * @param onClick Invoked when the item is clicked.
 * @param modifier The Modifier param
 * @param thumbnailSizePx The size of the tab's thumbnail in pixels.
 * @param selectionState: The tab's selection state.
 * @param shouldClickListen Whether or not the item should stop listening to click events.
 * @param onLongClick Invoked when the item is long clicked.
 * @param itemInfo: Optional CollectionItemInfo? for a11y to read this item in a collection.
 * @param accessibilityActions Accessibility actions offered on the item, such as reordering it.
 */
@Composable
fun TabGridTabItem(
    tab: TabsTrayItem.Tab,
    swipeToDismissBoxState: SwipeToDismissBoxState,
    swipingEnabled: Boolean,
    interactionState: TabItemInteractionState,
    onCloseClick: (TabsTrayItem.Tab) -> Unit,
    onClick: (TabsTrayItem) -> Unit,
    modifier: Modifier = Modifier,
    thumbnailSizePx: Int = 50,
    selectionState: TabsTrayItemSelectionState =
        TabsTrayItemSelectionState(
            multiSelectEnabled = false,
            isSelected = false,
            isFocused = false,
        ),
    shouldClickListen: Boolean = true,
    onLongClick: ((TabsTrayItem) -> Unit)? = null,
    itemInfo: CollectionItemInfo? = null,
    accessibilityActions: List<CustomAccessibilityAction> = emptyList(),
) {
    // SwipeToDismissBox invokes onDismiss from a LaunchedEffect keyed on the callback, so an
    // unstable lambda would re-close the tab on every recomposition that follows the dismissal.
    val currentTab by rememberUpdatedState(tab)
    val currentOnCloseClick by rememberUpdatedState(onCloseClick)
    val onDismiss = remember { { _: SwipeToDismissBoxValue -> currentOnCloseClick(currentTab) } }

    SwipeToDismissBox(
        modifier = modifier,
        state = swipeToDismissBoxState,
        backgroundContent = {},
        gesturesEnabled = swipingEnabled,
        onDismiss = onDismiss,
    ) {
        TabContent(
            modifier = Modifier.fadeOnSwipeToDismiss(swipeToDismissBoxState),
            tab = tab,
            thumbnailSize = thumbnailSizePx,
            selectionState = selectionState,
            clickHandler =
                TabsTrayItemClickHandler(
                    enabled = shouldClickListen,
                    onClick = onClick,
                    onLongClick = onLongClick,
                ),
            onCloseTabClick = onCloseClick,
            interactionState = interactionState,
            itemInfo = itemInfo,
            accessibilityActions = accessibilityActions,
        )
    }
}

/**
 * Preview for TabContent.
 *
 * @param tab: The [TabSessionState] data
 * @param thumbnailSize: The thumbnail's size in px
 * @param modifier: The Modifier param
 * @param selectionState: The tab's selection state - active, multi-selection, etc.
 * @param clickHandler: The tab's click handler,
 * @param onCloseTabClick: Invoked when a tab is closed.
 * @param interactionState The tab item's interaction state (hover, drag, etc)
 * @param itemInfo: Optional CollectionItemInfo? for a11y to read this item in a collection.
 * @param accessibilityActions Accessibility actions offered on the item, such as reordering it.
 */
@Composable
private fun TabContent(
    tab: TabsTrayItem.Tab,
    thumbnailSize: Int,
    modifier: Modifier = Modifier,
    selectionState: TabsTrayItemSelectionState =
        TabsTrayItemSelectionState(
            multiSelectEnabled = false,
            isFocused = false,
            isSelected = false,
        ),
    clickHandler: TabsTrayItemClickHandler,
    onCloseTabClick: ((TabsTrayItem.Tab) -> Unit),
    interactionState: TabItemInteractionState,
    itemInfo: CollectionItemInfo? = null,
    accessibilityActions: List<CustomAccessibilityAction> = emptyList(),
) {
    Box(
        modifier =
            modifier
                .wrapContentSize()
                .tabItemGridInteractionAnimation(interactionState = interactionState)
                .testTag(TabsTrayTestTag.TAB_ITEM_ROOT)
    ) {
        Card(
            modifier =
                Modifier.fillMaxWidth()
                    .clip(tabContentCardShape)
                    .tabItemClickable(
                        clickHandler = clickHandler,
                        clickedItem = tab,
                    )
                    .semantics {
                        selected = selectionState.isFocused
                        if (itemInfo != null) collectionItemInfo = itemInfo
                        if (accessibilityActions.isNotEmpty()) {
                            customActions = accessibilityActions
                        }
                    },
            shape = tabContentCardShape,
            border = tabItemConditionalBorder(selectionState),
            colors = CardDefaults.cardColors(containerColor = tabGridItemContainerColor(selectionState)),
        ) {
            Column(modifier = Modifier.aspectRatio(gridItemAspectRatio)) {
                Header(
                    tab = tab,
                    selectionState = selectionState,
                    onCloseClick = onCloseTabClick,
                )

                Spacer(modifier = Modifier.height(FirefoxTheme.layout.space.static25))

                Card(
                    modifier =
                        Modifier.padding(
                            start = FirefoxTheme.layout.space.static50,
                            end = FirefoxTheme.layout.space.static50,
                            bottom = FirefoxTheme.layout.space.static50,
                        ),
                    shape = thumbnailShape,
                ) {
                    Thumbnail(
                        tab = tab,
                        size = thumbnailSize,
                    )
                }

                Spacer(modifier = Modifier.height(FirefoxTheme.layout.space.static50))
            }
        }
    }
}

@Composable
private fun Header(
    tab: TabsTrayItem.Tab,
    selectionState: TabsTrayItemSelectionState,
    onCloseClick: (TabsTrayItem.Tab) -> Unit,
) {
    Row(
        modifier = Modifier.fillMaxWidth().wrapContentHeight(),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Spacer(modifier = Modifier.width(FirefoxTheme.layout.space.static100))

        TabIcon(tab = tab)

        Spacer(modifier = Modifier.width(FirefoxTheme.layout.space.static50))

        Text(
            text = tab.title.take(MAX_URI_LENGTH),
            modifier = Modifier.weight(1f),
            color = MaterialTheme.colorScheme.onSurface,
            maxLines = 1,
            overflow = TextOverflow.Ellipsis,
            style = FirefoxTheme.typography.caption,
        )

        Spacer(modifier = Modifier.width(FirefoxTheme.layout.space.static50))

        UtilityIcon(
            tab = tab,
            selectionState = selectionState,
            onCloseClick = onCloseClick,
        )
    }
}

@Composable
private fun TabIcon(tab: TabsTrayItem.Tab) {
    val icon = tab.icon
    if (icon != null) {
        icon.prepareToDraw()
        Image(
            bitmap = icon.asImageBitmap(),
            contentDescription = null,
            modifier = Modifier.size(TabHeaderFaviconSize),
        )
    } else if (tab.url == ABOUT_HOME_URL) {
        Image(
            painter = painterResource(id = R.drawable.ic_firefox),
            contentDescription = null,
            modifier = Modifier.size(TabHeaderFaviconSize),
        )
    } else {
        Favicon(
            url = tab.url,
            size = TabHeaderFaviconSize,
            isPrivate = tab.private,
        )
    }
}

@Composable
private fun UtilityIcon(
    tab: TabsTrayItem.Tab,
    selectionState: TabsTrayItemSelectionState,
    onCloseClick: (TabsTrayItem.Tab) -> Unit,
) {
    if (!selectionState.multiSelectEnabled) {
        CloseButton(
            tab = tab,
            onCloseClick = onCloseClick,
        )
    } else {
        Box(
            modifier = Modifier.size(TabHeaderIconTouchTargetSize),
            contentAlignment = Alignment.Center,
        ) {
            MultiSelectTabButton(isSelected = selectionState.isSelected)
        }
    }
}

@Composable
private fun CloseButton(
    tab: TabsTrayItem.Tab,
    onCloseClick: (TabsTrayItem.Tab) -> Unit,
) {
    IconButton(
        onClick = {
            onCloseClick(tab)
        },
        contentDescription =
            stringResource(
                id = R.string.close_tab_title,
                tab.title,
            ),
        modifier = Modifier.size(TabHeaderIconTouchTargetSize).testTag(TabsTrayTestTag.TAB_ITEM_CLOSE),
    ) {
        Icon(
            painter = painterResource(id = iconsR.drawable.mozac_ic_cross_20),
            contentDescription = null,
            tint = MaterialTheme.colorScheme.onSurface,
        )
    }
}

/**
 * Thumbnail specific for the [TabGridItem], which can be selected.
 *
 * @param tab Tab, containing the thumbnail to be displayed.
 * @param size Size of the thumbnail.
 */
@Composable
private fun Thumbnail(
    tab: TabsTrayItem.Tab,
    size: Int,
) {
    TabThumbnail(
        tabThumbnailImageData = tab.toThumbnailImageData(),
        thumbnailSizePx = size,
        modifier =
            Modifier.semantics(mergeDescendants = true) {
                    testTag = TabsTrayTestTag.TAB_ITEM_THUMBNAIL
                }
                .fillMaxSize(),
        shape = thumbnailShape,
    )
}

private data class TabGridItemPreviewState(
    val isActive: Boolean,
    val multiSelectionEnabled: Boolean,
    val multiSelectionSelected: Boolean,
    val url: String = "www.mozilla.org",
    val title: String = "Mozilla Domain",
    val interactionState: TabItemInteractionState = TabItemInteractionState(),
)

private val tabGridItemPreviewStateData: List<Pair<String, TabGridItemPreviewState>> =
    listOf(
        Pair(
            "No selection",
            TabGridItemPreviewState(
                isActive = false,
                multiSelectionEnabled = false,
                multiSelectionSelected = false,
            ),
        ),
        Pair(
            "Active tab",
            TabGridItemPreviewState(
                isActive = true,
                multiSelectionEnabled = false,
                multiSelectionSelected = false,
            ),
        ),
        Pair(
            "Selected tab with multi-select disabled",
            TabGridItemPreviewState(
                isActive = false,
                multiSelectionEnabled = true,
                multiSelectionSelected = false,
            ),
        ),
        Pair(
            "Active tab with multi-select enabled",
            TabGridItemPreviewState(
                isActive = true,
                multiSelectionEnabled = true,
                multiSelectionSelected = false,
            ),
        ),
        Pair(
            "No selection, multi-select enabled",
            TabGridItemPreviewState(
                isActive = false,
                multiSelectionEnabled = true,
                multiSelectionSelected = true,
            ),
        ),
        Pair(
            "Active, selected, multi-select enabled",
            TabGridItemPreviewState(
                isActive = true,
                multiSelectionEnabled = true,
                multiSelectionSelected = true,
            ),
        ),
        Pair(
            "Very long title",
            TabGridItemPreviewState(
                isActive = false,
                multiSelectionEnabled = false,
                multiSelectionSelected = false,
                url = "www.google.com/superlongurl",
                title = "Super super super super super super super super long title",
            ),
        ),
        Pair(
            "Dragged tab item",
            TabGridItemPreviewState(
                isActive = false,
                multiSelectionEnabled = false,
                multiSelectionSelected = false,
                interactionState = TabItemInteractionState(isDragged = true),
            ),
        ),
        Pair(
            "Hovered by item",
            TabGridItemPreviewState(
                isActive = false,
                multiSelectionEnabled = false,
                multiSelectionSelected = false,
                interactionState = TabItemInteractionState(isHoveredByItem = true),
            ),
        ),
    )

private class TabGridItemParameterProvider :
    ThemedValueProvider<TabGridItemPreviewState>(
        baseValues = tabGridItemPreviewStateData.map { it.second }.asSequence(),
        getDisplayName = { index, _ -> tabGridItemPreviewStateData[index].first },
    )

@Composable
@PreviewLightDark
private fun TabGridItemPreview(
    @PreviewParameter(TabGridItemParameterProvider::class) tabGridItemState: ThemedValue<TabGridItemPreviewState>
) {
    FirefoxTheme(theme = tabGridItemState.theme) {
        TabContent(
            tab =
                createTab(
                    url = tabGridItemState.value.url,
                    title = tabGridItemState.value.title,
                ),
            selectionState =
                TabsTrayItemSelectionState(
                    isSelected = tabGridItemState.value.multiSelectionSelected,
                    isFocused = tabGridItemState.value.isActive,
                    multiSelectEnabled = tabGridItemState.value.multiSelectionEnabled,
                ),
            thumbnailSize = 108,
            clickHandler = TabsTrayItemClickHandler(onClick = {}, onCloseClick = {}),
            onCloseTabClick = {},
            interactionState = tabGridItemState.value.interactionState,
        )
    }
}
