/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.tabgroups

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.BoxWithConstraintsScope
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.aspectRatio
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.wrapContentHeight
import androidx.compose.foundation.layout.wrapContentSize
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.LocalContentColor
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.material3.rememberSwipeToDismissBoxState
import androidx.compose.runtime.Composable
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.runtime.ReadOnlyComposable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.RectangleShape
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.semantics.CollectionItemInfo
import androidx.compose.ui.semantics.CustomAccessibilityAction
import androidx.compose.ui.semantics.collectionItemInfo
import androidx.compose.ui.semantics.customActions
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.tooling.preview.PreviewLightDark
import androidx.compose.ui.tooling.preview.PreviewParameter
import androidx.compose.ui.tooling.preview.PreviewParameterProvider
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import mozilla.components.browser.state.state.createTab
import mozilla.components.compose.base.annotation.FlexibleWindowLightDarkPreview
import mozilla.components.compose.base.theme.surfaceDimVariant
import mozilla.components.support.base.utils.MAX_URI_LENGTH
import mozilla.components.ui.colors.NovaColors
import org.mozilla.fenix.compose.TabThumbnail
import org.mozilla.fenix.compose.TabThumbnailImageData
import org.mozilla.fenix.tabstray.TabsTrayTestTag
import org.mozilla.fenix.tabstray.TabsTrayTestTag.TAB_GROUP_TITLE
import org.mozilla.fenix.tabstray.browser.compose.TabItemInteractionState
import org.mozilla.fenix.tabstray.data.TabGroupTheme
import org.mozilla.fenix.tabstray.data.TabsTrayItem
import org.mozilla.fenix.tabstray.ui.tabitems.LOREM_IPSUM
import org.mozilla.fenix.tabstray.ui.tabitems.MultiSelectTabButton
import org.mozilla.fenix.tabstray.ui.tabitems.TabGridTabItem
import org.mozilla.fenix.tabstray.ui.tabitems.TabGroupMenuButton
import org.mozilla.fenix.tabstray.ui.tabitems.TabHeaderIconTouchTargetSize
import org.mozilla.fenix.tabstray.ui.tabitems.TabsTrayItemClickHandler
import org.mozilla.fenix.tabstray.ui.tabitems.TabsTrayItemSelectionState
import org.mozilla.fenix.tabstray.ui.tabitems.gridItemAspectRatio
import org.mozilla.fenix.tabstray.ui.tabitems.tabContentCardShape
import org.mozilla.fenix.tabstray.ui.tabitems.tabGridItemContainerColor
import org.mozilla.fenix.tabstray.ui.tabitems.tabItemClickable
import org.mozilla.fenix.tabstray.ui.tabitems.tabItemConditionalBorder
import org.mozilla.fenix.tabstray.ui.tabitems.tabItemGridInteractionAnimation
import org.mozilla.fenix.tabstray.ui.tabitems.thumbnailShape
import org.mozilla.fenix.theme.FirefoxTheme

const val TOP_START_THUMBNAIL_INDEX = 0
const val TOP_END_THUMBNAIL_INDEX = 1
const val BOTTOM_START_THUMBNAIL_INDEX = 2
const val BOTTOM_END_THUMBNAIL_INDEX = 3

/**
 * A Tab Group presented as a clickable item in a grid.
 *
 * @param group: The data of the [TabsTrayItem.TabGroup].
 * @param selectionState: The tab selection state.
 * @param clickHandler: Handler for all click-handling inputs (long click, click, etc)
 * @param modifier: The Modifier
 * @param interactionState The tab item's interaction state (hover, drag, etc)
 * @param onEditTabGroupClick Invoked when the user clicks to edit the tab group.
 * @param onCloseTabGroupClick Invoked when the user clicks to close the tab group.
 * @param onShareTabGroupClick Invoked when the user clicks to share the tab group.
 * @param onDeleteTabGroupClick Invoked when the user clicks on delete tab group.
 * @param itemInfo: Optional CollectionItemInfo? for reading this item in a list.
 * @param accessibilityActions Accessibility actions offered on the item, such as reordering it.
 */
@Suppress("LongParameterList")
@Composable
fun TabGroupCard(
    group: TabsTrayItem.TabGroup,
    selectionState: TabsTrayItemSelectionState,
    clickHandler: TabsTrayItemClickHandler,
    modifier: Modifier = Modifier,
    interactionState: TabItemInteractionState,
    onEditTabGroupClick: () -> Unit,
    onCloseTabGroupClick: () -> Unit,
    onShareTabGroupClick: (TabsTrayItem.TabGroup) -> Unit,
    onDeleteTabGroupClick: (TabsTrayItem.TabGroup) -> Unit,
    itemInfo: CollectionItemInfo? = null,
    accessibilityActions: List<CustomAccessibilityAction> = emptyList(),
) {
    val containerColor = tabGridItemContainerColor(selectionState)

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
                        clickedItem = group,
                    )
                    .semantics {
                        if (itemInfo != null) {
                            collectionItemInfo = itemInfo
                        }
                        if (accessibilityActions.isNotEmpty()) {
                            customActions = accessibilityActions
                        }
                    },
            shape = tabContentCardShape,
            border = tabItemConditionalBorder(selectionState),
            colors = CardDefaults.cardColors(containerColor = containerColor),
        ) {
            Column(modifier = Modifier.aspectRatio(gridItemAspectRatio)) {
                // Title Row
                Row(
                    modifier = Modifier.background(color = group.theme.primary).fillMaxWidth().wrapContentHeight(),
                    verticalAlignment = Alignment.CenterVertically,
                ) {
                    CompositionLocalProvider(LocalContentColor provides group.theme.onPrimary) {
                        Spacer(modifier = Modifier.width(FirefoxTheme.layout.space.static100))

                        Text(
                            text = group.title.take(MAX_URI_LENGTH),
                            modifier = Modifier.weight(1f).testTag(TAB_GROUP_TITLE),
                            maxLines = 1,
                            overflow = TextOverflow.Ellipsis,
                            style = FirefoxTheme.typography.caption,
                        )

                        Spacer(modifier = Modifier.width(FirefoxTheme.layout.space.static50))

                        TabGroupOptionButton(
                            selectionState = selectionState,
                            onEditTabGroupClick = onEditTabGroupClick,
                            onCloseTabGroupClick = onCloseTabGroupClick,
                            onShareTabGroupClick = { onShareTabGroupClick(group) },
                            onDeleteTabGroupClick = { onDeleteTabGroupClick(group) },
                        )
                    }
                }

                Spacer(modifier = Modifier.height(FirefoxTheme.layout.space.static25))

                // 4x4 Thumbnail Grid
                Card(
                    modifier =
                        Modifier.padding(
                            start = FirefoxTheme.layout.space.static50,
                            end = FirefoxTheme.layout.space.static50,
                            bottom = FirefoxTheme.layout.space.static50,
                        ),
                    shape = thumbnailShape,
                ) {
                    ThumbnailsGridView(
                        thumbnails = group.thumbnails,
                        containerColor = containerColor,
                    )
                }

                Spacer(modifier = Modifier.height(FirefoxTheme.layout.space.static50))
            }
        }
    }
}

/** Renders the button in the top-right corner of the TabGroupCard. */
@Composable
private fun TabGroupOptionButton(
    selectionState: TabsTrayItemSelectionState,
    onEditTabGroupClick: () -> Unit,
    onCloseTabGroupClick: () -> Unit,
    onShareTabGroupClick: () -> Unit,
    onDeleteTabGroupClick: () -> Unit,
) {
    if (selectionState.multiSelectEnabled) {
        MultiSelectTabButton(
            isSelected = selectionState.isSelected,
            uncheckedBorderColor = LocalContentColor.current,
        )
    } else {
        TabGroupMenuButton(
            modifier = Modifier.size(TabHeaderIconTouchTargetSize),
            includeCloseOption = true,
            includeUngroupOption = true,
            onDeleteTabGroupClick = onDeleteTabGroupClick,
            onEditTabGroupClick = onEditTabGroupClick,
            onCloseTabGroupClick = onCloseTabGroupClick,
            onShareTabGroupClick = onShareTabGroupClick,
            onUngroupTabGroupClick = {},
        )
    }
}

/**
 * Determines the dimensions of the group thumbnails, which should each occupy one quarter of a thumbnail's size, by
 * halving each dimension and subtracting the required padding. Returns the results as a Pair with the first element the
 * width in Dp and the second element the height in Dp.
 */
private val BoxWithConstraintsScope.groupThumbnailDimens: ThumbnailDimensions
    @ReadOnlyComposable
    @Composable
    get() {
        return ThumbnailDimensions(
            width = Width((this.maxWidth - FirefoxTheme.layout.space.static25) / 2),
            height = Height((this.maxHeight - FirefoxTheme.layout.space.static25) / 2),
        )
    }

private val BoxWithConstraintsScope.groupThumbnailSizePx: Int
    @ReadOnlyComposable
    @Composable
    get() {
        return with(LocalDensity.current) {
            maxOf(groupThumbnailDimens.width, groupThumbnailDimens.height).roundToPx()
        }
    }

/**
 * Renders up to 4 Tab thumbnails in a 2x2 grid. Note that the aspect ratio is not set because these thumbnails size
 * themselves to fit the available space.
 *
 * @param thumbnails: List of thumbnails. May be empty, or up to size 4.
 * @param modifier: Modifier parameter
 * @param containerColor: Background Color of the thumbnails grid.
 */
@Composable
fun ThumbnailsGridView(
    thumbnails: List<TabThumbnailImageData>,
    modifier: Modifier = Modifier,
    containerColor: Color = MaterialTheme.colorScheme.surfaceContainerHighest,
) {
    BoxWithConstraints {
        val groupThumbnailDimens = groupThumbnailDimens
        val thumbnailSizePx = groupThumbnailSizePx
        Column(
            modifier = modifier.fillMaxWidth().background(color = containerColor),
            verticalArrangement = Arrangement.spacedBy(FirefoxTheme.layout.space.static25),
        ) {
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.spacedBy(FirefoxTheme.layout.space.static25),
            ) {
                TabGroupThumbnail(
                    tabThumbnailImageData = thumbnails.getOrNull(TOP_START_THUMBNAIL_INDEX),
                    thumbnailSizePx = thumbnailSizePx,
                    modifier =
                        Modifier.width(groupThumbnailDimens.width)
                            .height(groupThumbnailDimens.height)
                            .testTag(TabsTrayTestTag.TAB_GROUP_THUMBNAIL_FIRST),
                )
                TabGroupThumbnail(
                    tabThumbnailImageData = thumbnails.getOrNull(TOP_END_THUMBNAIL_INDEX),
                    thumbnailSizePx = thumbnailSizePx,
                    modifier =
                        Modifier.width(groupThumbnailDimens.width)
                            .height(groupThumbnailDimens.height)
                            .testTag(TabsTrayTestTag.TAB_GROUP_THUMBNAIL_SECOND),
                )
            }
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.spacedBy(FirefoxTheme.layout.space.static25),
            ) {
                TabGroupThumbnail(
                    tabThumbnailImageData = thumbnails.getOrNull(BOTTOM_START_THUMBNAIL_INDEX),
                    thumbnailSizePx = thumbnailSizePx,
                    modifier =
                        Modifier.width(groupThumbnailDimens.width)
                            .height(groupThumbnailDimens.height)
                            .testTag(TabsTrayTestTag.TAB_GROUP_THUMBNAIL_THIRD),
                )
                TabGroupThumbnail(
                    tabThumbnailImageData = thumbnails.getOrNull(BOTTOM_END_THUMBNAIL_INDEX),
                    thumbnailSizePx = thumbnailSizePx,
                    modifier =
                        Modifier.width(groupThumbnailDimens.width)
                            .height(groupThumbnailDimens.height)
                            .testTag(TabsTrayTestTag.TAB_GROUP_THUMBNAIL_FOURTH),
                )
            }
        }
    }
}

/**
 * A ExpandedTabGroup has anywhere from 0 to 4 thumbnail images. Renders a thumbnail image if thumbnail image data is
 * available, or an empty box if the thumbnail image data is null.
 *
 * @param tabThumbnailImageData: thumbnail image data (may be null)
 * @param thumbnailSizePx: the size of each thumbnail in px
 * @param modifier: The modifier
 */
@Composable
private fun TabGroupThumbnail(
    tabThumbnailImageData: TabThumbnailImageData?,
    thumbnailSizePx: Int,
    modifier: Modifier = Modifier,
) {
    if (tabThumbnailImageData != null) {
        TabThumbnail(
            tabThumbnailImageData = tabThumbnailImageData,
            thumbnailSizePx = thumbnailSizePx,
            shape = RectangleShape,
            modifier = modifier,
        )
    } else {
        Box(modifier = modifier.background(color = MaterialTheme.colorScheme.surfaceDimVariant))
    }
}

private data class TabGroupCardPreviewState(
    val title: String = "Group 1",
    val color: Color = NovaColors.Pink70,
    val selectionState: TabsTrayItemSelectionState =
        TabsTrayItemSelectionState(
            isSelected = false,
            isFocused = false,
            multiSelectEnabled = false,
        ),
    val groupSize: Int,
    val group: TabsTrayItem.TabGroup =
        TabsTrayItem.TabGroup(
            title = "Tab Group Item",
            theme = TabGroupTheme.default,
            closed = false,
            tabs =
                List(groupSize) { index ->
                        TabsTrayItem.Tab(
                            id = "Tab$index",
                            title = "Tab $index",
                            url = "mozilla.org",
                            inactive = false,
                            private = false,
                            icon = null,
                            lastAccess = 0L,
                            isFocused = false,
                        )
                    }
                    .toMutableList(),
        ),
    val interactionState: TabItemInteractionState = TabItemInteractionState(),
)

private class TabGroupCardPreviewProvider : PreviewParameterProvider<TabGroupCardPreviewState> {
    val data =
        listOf(
            Pair("Empty", TabGroupCardPreviewState(groupSize = 0)),
            Pair("1 Tab", TabGroupCardPreviewState(groupSize = 1)),
            Pair("2 Tabs", TabGroupCardPreviewState(groupSize = 2)),
            Pair("3 Tabs", TabGroupCardPreviewState(groupSize = 3)),
            Pair("4 Tabs", TabGroupCardPreviewState(groupSize = 4)),
            Pair(
                "No Title",
                TabGroupCardPreviewState(title = "", groupSize = 4),
            ),
            Pair(
                "Long Title",
                TabGroupCardPreviewState(title = LOREM_IPSUM, groupSize = 4),
            ),
            Pair(
                "Active",
                TabGroupCardPreviewState(
                    selectionState =
                        TabsTrayItemSelectionState(
                            isFocused = true,
                            isSelected = false,
                            multiSelectEnabled = false,
                        ),
                    groupSize = 4,
                ),
            ),
            Pair(
                "Select mode",
                TabGroupCardPreviewState(
                    selectionState =
                        TabsTrayItemSelectionState(
                            isFocused = false,
                            isSelected = false,
                            multiSelectEnabled = true,
                        ),
                    groupSize = 4,
                ),
            ),
            Pair(
                "Selected",
                TabGroupCardPreviewState(
                    selectionState =
                        TabsTrayItemSelectionState(
                            isFocused = false,
                            isSelected = true,
                            multiSelectEnabled = true,
                        ),
                    groupSize = 4,
                ),
            ),
            Pair(
                "Active selected",
                TabGroupCardPreviewState(
                    selectionState =
                        TabsTrayItemSelectionState(
                            isFocused = true,
                            isSelected = true,
                            multiSelectEnabled = true,
                        ),
                    groupSize = 4,
                ),
            ),
            Pair(
                "Dragged",
                TabGroupCardPreviewState(
                    selectionState =
                        TabsTrayItemSelectionState(
                            isFocused = false,
                            isSelected = false,
                            multiSelectEnabled = false,
                        ),
                    groupSize = 4,
                    interactionState = TabItemInteractionState(isDragged = true),
                ),
            ),
            Pair(
                "Hovered by item",
                TabGroupCardPreviewState(
                    selectionState =
                        TabsTrayItemSelectionState(
                            isFocused = false,
                            isSelected = false,
                            multiSelectEnabled = false,
                        ),
                    groupSize = 4,
                    interactionState = TabItemInteractionState(isHoveredByItem = true),
                ),
            ),
        )

    override val values: Sequence<TabGroupCardPreviewState>
        get() = data.map { it.second }.asSequence()

    override fun getDisplayName(index: Int): String {
        return data[index].first
    }
}

private class ThumbnailsGridViewPreviewProvider : PreviewParameterProvider<List<TabThumbnailImageData>> {
    val data =
        listOf(
            Pair("Empty", emptyList()),
            Pair("1 Tab", fakeThumbnails(1)),
            Pair("2 Tabs", fakeThumbnails(2)),
            Pair("3 Tabs", fakeThumbnails(3)),
            Pair("4 Tabs", fakeThumbnails(4)),
        )
    override val values: Sequence<List<TabThumbnailImageData>>
        get() = data.map { it.second }.asSequence()

    override fun getDisplayName(index: Int): String {
        return data[index].first
    }
}

@PreviewLightDark
@Composable
private fun ThumbnailsGridViewPreview(
    @PreviewParameter(ThumbnailsGridViewPreviewProvider::class) thumbnails: List<TabThumbnailImageData>
) {
    FirefoxTheme {
        Box(modifier = Modifier.size(100.dp)) {
            ThumbnailsGridView(thumbnails = thumbnails)
        }
    }
}

// Groups are not supported in Private mode.
@FlexibleWindowLightDarkPreview
@Composable
private fun TabGroupCardPreview(
    @PreviewParameter(TabGroupCardPreviewProvider::class) tabGroupCardState: TabGroupCardPreviewState
) {
    FirefoxTheme {
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.spacedBy(6.dp),
            verticalAlignment = Alignment.Top,
        ) {
            TabGridTabItem(
                tab =
                    TabsTrayItem.Tab(
                        createTab(
                            url = "about:home",
                            title = "Kit's Blog",
                        )
                    ),
                swipeToDismissBoxState = rememberSwipeToDismissBoxState(),
                swipingEnabled = true,
                interactionState = tabGroupCardState.interactionState,
                onCloseClick = {},
                onClick = {},
                modifier = Modifier.weight(1f),
                thumbnailSizePx = 50,
                selectionState = tabGroupCardState.selectionState,
            )

            TabGroupCard(
                group = tabGroupCardState.group,
                selectionState = tabGroupCardState.selectionState,
                clickHandler =
                    TabsTrayItemClickHandler(
                        enabled = true,
                        onClick = { _: TabsTrayItem -> },
                        onCloseClick = { _: TabsTrayItem -> },
                        onLongClick = { _: TabsTrayItem -> },
                    ),
                modifier = Modifier.weight(1f),
                interactionState = tabGroupCardState.interactionState,
                onEditTabGroupClick = {},
                onCloseTabGroupClick = {},
                onShareTabGroupClick = {},
                onDeleteTabGroupClick = {},
            )
        }
    }
}

internal fun fakeThumbnails(limit: Int = 4): List<TabThumbnailImageData> {
    return listOf(
            TabThumbnailImageData(
                tabId = "1",
                isPrivate = false,
                tabUrl = "mozilla.org",
                tabIcon = null,
            ),
            TabThumbnailImageData(
                tabId = "1",
                isPrivate = false,
                tabUrl = "mozilla.org",
                tabIcon = null,
            ),
            TabThumbnailImageData(
                tabId = "1",
                isPrivate = false,
                tabUrl = "mozilla.org",
                tabIcon = null,
            ),
            TabThumbnailImageData(
                tabId = "1",
                isPrivate = false,
                tabUrl = "mozilla.org",
                tabIcon = null,
            ),
        )
        .subList(0, limit)
}

@JvmInline private value class Width(val width: Dp)

@JvmInline private value class Height(val height: Dp)

@JvmInline
private value class ThumbnailDimensions(private val dimensions: Pair<Width, Height>) {
    constructor(width: Width, height: Height) : this(width to height)

    val width: Dp
        get() = this.dimensions.first.width

    val height: Dp
        get() = this.dimensions.second.height
}
