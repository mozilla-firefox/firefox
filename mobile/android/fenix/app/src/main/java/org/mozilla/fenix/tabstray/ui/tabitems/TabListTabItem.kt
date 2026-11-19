/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.tabstray.ui.tabitems

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.SwipeToDismissBox
import androidx.compose.material3.SwipeToDismissBoxValue
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.ReadOnlyComposable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.selected
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.tooling.preview.PreviewLightDark
import androidx.compose.ui.tooling.preview.PreviewParameter
import androidx.compose.ui.tooling.preview.PreviewParameterProvider
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import mozilla.components.browser.tabstray.R as tabstrayR
import mozilla.components.compose.base.RadioCheckmark
import mozilla.components.compose.draggable.ItemInteractionState
import mozilla.components.support.base.utils.MAX_URI_LENGTH
import org.mozilla.fenix.R
import org.mozilla.fenix.compose.SwipeToDismissBackground
import org.mozilla.fenix.compose.TabThumbnail
import org.mozilla.fenix.compose.rememberSwipeToDismissBoxState
import org.mozilla.fenix.compose.swipeToDismissFade
import org.mozilla.fenix.ext.toShortUrl
import org.mozilla.fenix.tabstray.TabsTrayTestTag
import org.mozilla.fenix.tabstray.data.TabsTrayItem
import org.mozilla.fenix.tabstray.data.createTab
import org.mozilla.fenix.tabstray.telemetry.OnTabClose
import org.mozilla.fenix.tabstray.telemetry.TabCloseType
import org.mozilla.fenix.theme.FirefoxTheme

private val ThumbnailWidth = 78.dp
private val ThumbnailHeight = 68.dp
internal val TabListItemHeight: Dp
    @Composable @ReadOnlyComposable get() = ThumbnailHeight + FirefoxTheme.layout.space.static100 * 2

/**
 * List item used to display a tab that supports clicks, long clicks, multiselection, and media controls.
 *
 * @param tab The given tab to render as list item.
 * @param modifier [Modifier] to be applied to the tab list item content.
 * @param interactionState: [ItemInteractionState] holding hovered and dragged status.
 * @param selectionState: The tab item's [TabsTrayItemSelectionState]
 * @param shouldClickListen Whether the item should stop listening to click events.
 * @param swipingEnabled Whether the item is swipeable.
 * @param onTabClose Invoked when a gesture is performed to close a tab. The [TabsTrayItem.Tab] and [TabCloseType] are
 *   returned with the callback.
 * @param onClick Invoked when the item is clicked.
 * @param onLongClick Invoked when the item is long clicked.
 */
@Composable
fun TabListTabItem(
    tab: TabsTrayItem.Tab,
    modifier: Modifier = Modifier,
    interactionState: ItemInteractionState = ItemInteractionState(),
    selectionState: TabsTrayItemSelectionState = TabsTrayItemSelectionState(),
    shouldClickListen: Boolean = true,
    swipingEnabled: Boolean = true,
    onTabClose: OnTabClose,
    onClick: (TabsTrayItem) -> Unit,
    onLongClick: ((TabsTrayItem) -> Unit)? = null,
) {
    val swipeToDismissBoxState = rememberSwipeToDismissBoxState(id = tab.id)

    // SwipeToDismissBox invokes onDismiss from a LaunchedEffect keyed on the callback, so an
    // unstable lambda would re-close the tab on every recomposition that follows the dismissal.
    val currentTab by rememberUpdatedState(tab)
    val currentOnTabClose by rememberUpdatedState(onTabClose)
    val onDismiss = remember { { _: SwipeToDismissBoxValue -> currentOnTabClose(currentTab, TabCloseType.SWIPE) } }

    SwipeToDismissBox(
        state = swipeToDismissBoxState,
        onDismiss = onDismiss,
        gesturesEnabled = !selectionState.multiSelectEnabled && swipingEnabled,
        backgroundContent = {
            SwipeToDismissBackground(swipeToDismissBoxState)
        },
    ) {
        TabContent(
            tab = tab,
            selectionState = selectionState,
            interactionState = interactionState,
            shouldClickListen = shouldClickListen,
            // The fade has to wrap the caller's modifier: the focus outline is a border applied
            // there by tabListItemShapeStyling, and it only picks up the alpha if it is drawn
            // inside the layer.
            modifier = Modifier.swipeToDismissFade(swipeToDismissBoxState).then(modifier),
            onTabClose = onTabClose,
            onClick = onClick,
            onLongClick = onLongClick,
        )
    }
}

@Suppress("LongParameterList")
@Composable
private fun TabContent(
    tab: TabsTrayItem.Tab,
    interactionState: ItemInteractionState,
    selectionState: TabsTrayItemSelectionState,
    shouldClickListen: Boolean,
    modifier: Modifier = Modifier,
    onTabClose: OnTabClose,
    onClick: (TabsTrayItem) -> Unit,
    onLongClick: ((TabsTrayItem) -> Unit)? = null,
) {
    val contentBackgroundColor =
        if (selectionState.isSelected) {
            MaterialTheme.colorScheme.primaryContainer
        } else {
            MaterialTheme.colorScheme.surfaceBright
        }
    Row(
        modifier =
            modifier
                .fillMaxWidth()
                .tabItemListInteractionAnimation(interactionState = interactionState)
                .background(contentBackgroundColor)
                .tabItemClickable(
                    clickHandler =
                        TabsTrayItemClickHandler(
                            enabled = shouldClickListen,
                            onClick = onClick,
                            onLongClick = onLongClick,
                        ),
                    clickedItem = tab,
                )
                .padding(start = 16.dp, top = 8.dp, bottom = 8.dp)
                .testTag(TabsTrayTestTag.TAB_ITEM_ROOT)
                .semantics {
                    selected = selectionState.isFocused
                },
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Thumbnail(tab = tab)

        Column(modifier = Modifier.padding(start = 16.dp).weight(weight = 1f)) {
            Text(
                text = tab.title.take(MAX_URI_LENGTH),
                color = MaterialTheme.colorScheme.onSurface,
                style = FirefoxTheme.typography.body1,
                overflow = TextOverflow.Ellipsis,
                maxLines = 2,
            )

            Text(
                text = tab.url.toShortUrl(),
                color = MaterialTheme.colorScheme.secondary,
                style = FirefoxTheme.typography.body2,
                overflow = TextOverflow.Ellipsis,
                maxLines = 1,
            )
        }

        TabListIcon(
            selectionState = selectionState,
            onTabClose = onTabClose,
            tab = tab,
        )
    }
}

@Composable
private fun TabListIcon(
    selectionState: TabsTrayItemSelectionState,
    onTabClose: OnTabClose,
    tab: TabsTrayItem.Tab,
) {
    if (!selectionState.multiSelectEnabled) {
        ListItemDismissButton(
            contentDescription =
                stringResource(
                    id = R.string.close_tab_title,
                    tab.title,
                ),
            modifier = Modifier.testTag(TabsTrayTestTag.TAB_ITEM_CLOSE),
            onClick = { onTabClose(tab, TabCloseType.TAP) },
        )
    } else {
        RadioCheckmark(
            isSelected = selectionState.isSelected,
            modifier = Modifier.padding(end = FirefoxTheme.layout.space.dynamic200),
        )
    }
}

@Composable
private fun Thumbnail(tab: TabsTrayItem.Tab) {
    val density = LocalDensity.current
    val thumbnailSize = with(density) { ThumbnailWidth.toPx() }.toInt()
    Box(
        modifier =
            Modifier.size(
                width = ThumbnailWidth,
                height = ThumbnailHeight,
            )
    ) {
        TabThumbnail(
            tabThumbnailImageData = tab.toThumbnailImageData(),
            thumbnailSizePx = thumbnailSize,
            modifier = Modifier.fillMaxSize().testTag(TabsTrayTestTag.TAB_ITEM_THUMBNAIL),
            shape = MaterialTheme.shapes.extraSmall,
            border = tablistItemThumbnailBorder,
            contentDescription = stringResource(id = tabstrayR.string.mozac_browser_tabstray_open_tab),
        )

        MediaPlaybackIndicator(isMediaActive = tab.isMediaActive)
    }
}

private data class TabListItemPreviewState(
    val tabItemSelectionState: TabsTrayItemSelectionState,
    val url: String = "www.mozilla.org",
    val title: String = "Mozilla Domain",
    val itemInteractionState: ItemInteractionState = ItemInteractionState(),
)

private class TabListItemParameterProvider : PreviewParameterProvider<TabListItemPreviewState> {
    val data: List<Pair<String, TabListItemPreviewState>> =
        listOf(
            Pair(
                "Not focused or selected",
                TabListItemPreviewState(
                    tabItemSelectionState =
                        TabsTrayItemSelectionState(
                            isFocused = false,
                            multiSelectEnabled = false,
                            isSelected = false,
                        )
                ),
            ),
            Pair(
                "Focused, not selected",
                TabListItemPreviewState(
                    tabItemSelectionState =
                        TabsTrayItemSelectionState(
                            isFocused = true,
                            multiSelectEnabled = false,
                            isSelected = false,
                        )
                ),
            ),
            Pair(
                "Multiselection enabled, not focused or selected",
                TabListItemPreviewState(
                    tabItemSelectionState =
                        TabsTrayItemSelectionState(
                            isFocused = false,
                            multiSelectEnabled = true,
                            isSelected = false,
                        )
                ),
            ),
            Pair(
                "Multiselection enabled, focused, not selected",
                TabListItemPreviewState(
                    tabItemSelectionState =
                        TabsTrayItemSelectionState(
                            isFocused = true,
                            multiSelectEnabled = true,
                            isSelected = false,
                        )
                ),
            ),
            Pair(
                "Multiselection enabled, not focused, selected",
                TabListItemPreviewState(
                    tabItemSelectionState =
                        TabsTrayItemSelectionState(
                            isFocused = false,
                            multiSelectEnabled = true,
                            isSelected = true,
                        )
                ),
            ),
            Pair(
                "Multiselection enabled, focused and selected",
                TabListItemPreviewState(
                    tabItemSelectionState =
                        TabsTrayItemSelectionState(
                            isFocused = true,
                            multiSelectEnabled = true,
                            isSelected = true,
                        )
                ),
            ),
            Pair(
                "Not focused or selected, long title",
                TabListItemPreviewState(
                    tabItemSelectionState =
                        TabsTrayItemSelectionState(
                            isFocused = false,
                            multiSelectEnabled = false,
                            isSelected = false,
                        ),
                    url = "www.google.com/superlongurl",
                    title = LOREM_IPSUM,
                ),
            ),
            Pair(
                "Dragged",
                TabListItemPreviewState(
                    tabItemSelectionState =
                        TabsTrayItemSelectionState(
                            isFocused = false,
                            multiSelectEnabled = false,
                            isSelected = false,
                        ),
                    itemInteractionState =
                        ItemInteractionState(
                            isDragged = true,
                            isHoveredByItem = false,
                        ),
                ),
            ),
            Pair(
                "Hovered",
                TabListItemPreviewState(
                    tabItemSelectionState =
                        TabsTrayItemSelectionState(
                            isFocused = false,
                            multiSelectEnabled = false,
                            isSelected = false,
                        ),
                    itemInteractionState =
                        ItemInteractionState(
                            isDragged = false,
                            isHoveredByItem = true,
                        ),
                ),
            ),
        )

    override fun getDisplayName(index: Int): String? {
        return data[index].first
    }

    override val values: Sequence<TabListItemPreviewState>
        get() = data.map { it.second }.asSequence()
}

@Composable
@PreviewLightDark
private fun TabListTabItemPreview(
    @PreviewParameter(TabListItemParameterProvider::class) tabListItemState: TabListItemPreviewState
) {
    FirefoxTheme {
        TabListTabItem(
            tab =
                createTab(
                    url = tabListItemState.url,
                    title = tabListItemState.title,
                ),
            onTabClose = { _, _ -> },
            onClick = {},
            selectionState = tabListItemState.tabItemSelectionState,
            interactionState = tabListItemState.itemInteractionState,
        )
    }
}

@Composable
@PreviewLightDark
private fun TabListTabItemMediaPreview() {
    FirefoxTheme {
        TabListTabItem(
            tab =
                createTab(
                    url = "www.mozilla.org",
                    title = "Mozilla Domain",
                    isMediaActive = true,
                ),
            onTabClose = { _, _ -> },
            onClick = {},
        )
    }
}
