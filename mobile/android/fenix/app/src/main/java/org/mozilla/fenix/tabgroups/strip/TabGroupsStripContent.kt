/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.tabgroups.strip

import androidx.annotation.VisibleForTesting
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxScope
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyListScope
import androidx.compose.foundation.lazy.LazyListState
import androidx.compose.foundation.lazy.LazyRow
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.lazy.rememberLazyListState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.tooling.preview.Preview
import androidx.compose.ui.tooling.preview.PreviewParameter
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import mozilla.components.compose.base.button.IconButton
import mozilla.components.compose.base.theme.ThemedValue
import mozilla.components.compose.base.theme.ThemedValueProvider
import mozilla.components.ui.icons.R as iconsR
import org.mozilla.fenix.R
import org.mozilla.fenix.compose.Favicon
import org.mozilla.fenix.tabgroups.TabGroupsTestTag
import org.mozilla.fenix.tabgroups.strip.redux.TabGroupsStripAction
import org.mozilla.fenix.tabgroups.strip.redux.TabGroupsStripState
import org.mozilla.fenix.tabgroups.strip.redux.TabGroupsStripStore
import org.mozilla.fenix.theme.FirefoxTheme

/**
 * A horizontal strip of the active group's tabs, each shown as a favicon circle, optionally followed by an add tab
 * button, and an expand button.
 *
 * @param state The [TabGroupsStripState] to render.
 * @param listState The [LazyListState] holding the overflowing strip's scroll position.
 * @param onTabClick Invoked with a tab's ID when its favicon is tapped.
 * @param onAddTabClick Invoked when the add tab button is tapped.
 * @param onExpandGroupClick Invoked when the expand button is tapped.
 * @param modifier The [Modifier] to be applied to the strip.
 * @param showAddButton Whether the add tab button is shown. Hidden when homepage as a new tab (HNT) is disabled.
 */
@Composable
internal fun TabGroupsStripContent(
    state: TabGroupsStripState.TabGroupStripVisible,
    listState: LazyListState,
    onTabClick: (String) -> Unit,
    onAddTabClick: () -> Unit,
    onExpandGroupClick: () -> Unit,
    modifier: Modifier = Modifier,
    showAddButton: Boolean = true,
) {
    val edgePadding = FirefoxTheme.layout.space.static100
    val itemSpacing = FirefoxTheme.layout.space.static100
    val itemSize = FirefoxTheme.layout.size.static600
    val expandButtonWidth = FirefoxTheme.layout.size.static600

    BoxWithConstraints(
        modifier = modifier.fillMaxWidth().testTag(TabGroupsTestTag.TAB_GROUPS_STRIP),
        contentAlignment = Alignment.CenterStart,
    ) {
        val tabsOverflowing =
            remember(state.tabs.size, maxWidth, showAddButton) {
                areTabsOverflowing(
                    tabCount = state.tabs.size,
                    availableWidth = maxWidth,
                    itemSize = itemSize,
                    itemSpacing = itemSpacing,
                    edgePadding = edgePadding,
                    expandButtonWidth = expandButtonWidth,
                    showAddButton = showAddButton,
                )
            }

        if (tabsOverflowing) {
            OverflowingTabGroupsStripContent(
                state = state,
                listState = listState,
                showAddButton = showAddButton,
                onTabClick = onTabClick,
                onAddTabClick = onAddTabClick,
                onExpandGroupClick = onExpandGroupClick,
            )
        } else {
            Row(
                modifier = Modifier.fillMaxWidth().padding(end = expandButtonWidth + edgePadding),
                horizontalArrangement = Arrangement.spacedBy(itemSpacing),
                verticalAlignment = Alignment.CenterVertically,
            ) {
                LazyRow(
                    modifier = Modifier.weight(weight = 1f, fill = false),
                    horizontalArrangement = Arrangement.spacedBy(itemSpacing),
                    contentPadding = PaddingValues(start = edgePadding),
                    verticalAlignment = Alignment.CenterVertically,
                ) {
                    tabFaviconItems(state.tabs, onTabClick)
                }
                if (showAddButton) {
                    AddTabButton(onClick = onAddTabClick)
                }
            }

            ExpandButton(
                onClick = onExpandGroupClick,
                modifier = Modifier.align(Alignment.CenterEnd).padding(end = edgePadding),
            )
        }
    }
}

/** Lays out the group's tabs as favicon circles. */
private fun LazyListScope.tabFaviconItems(
    tabs: List<TabGroupsStripState.Tab>,
    onTabClick: (String) -> Unit,
) {
    items(items = tabs, key = { it.id }) { tab ->
        TabFaviconCircle(tab = tab, onClick = { onTabClick(tab.id) })
    }
}

/** The tabs scrolling behind the pinned add and expand buttons, used when the group's tabs overflow the strip. */
@Composable
private fun BoxScope.OverflowingTabGroupsStripContent(
    state: TabGroupsStripState.TabGroupStripVisible,
    listState: LazyListState,
    showAddButton: Boolean,
    onTabClick: (String) -> Unit,
    onAddTabClick: () -> Unit,
    onExpandGroupClick: () -> Unit,
) {
    val edgePadding = FirefoxTheme.layout.space.static100
    val itemSpacing = FirefoxTheme.layout.space.static100
    val itemSize = FirefoxTheme.layout.size.static600
    val expandButtonWidth = FirefoxTheme.layout.size.static600
    val addButtonWidth = if (showAddButton) itemSize else 0.dp

    val focusedIndex = state.tabs.indexOfFirst { it.isFocused }
    // Whenever the focused tab changes, scroll to the item if not fully visible.
    LaunchedEffect(focusedIndex) {
        if (!listState.isItemFullyVisible(focusedIndex)) {
            listState.animateScrollToItem(focusedIndex)
        }
    }

    LazyRow(
        state = listState,
        modifier = Modifier.fillMaxWidth(),
        horizontalArrangement = Arrangement.spacedBy(itemSpacing),
        contentPadding =
            PaddingValues(
                start = edgePadding,
                end = gradientWidth + addButtonWidth + expandButtonWidth + edgePadding,
            ),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        tabFaviconItems(state.tabs, onTabClick)
    }

    OverflowTrailingButtons(
        showAddButton = showAddButton,
        onAddTabClick = onAddTabClick,
        onExpandGroupClick = onExpandGroupClick,
        endPadding = edgePadding,
        modifier = Modifier.align(Alignment.CenterEnd),
    )
}

/** Whether the item at [index] is visible within the strip and list's viewport excluding end padding. */
private fun LazyListState.isItemFullyVisible(index: Int): Boolean {
    val item = layoutInfo.visibleItemsInfo.firstOrNull { it.index == index } ?: return false
    val start = layoutInfo.viewportStartOffset + layoutInfo.beforeContentPadding
    val end = layoutInfo.viewportEndOffset - layoutInfo.afterContentPadding
    return item.offset >= start && item.offset + item.size <= end
}

/**
 * Whether there are too many tabs in the group to fit before the pinned expand button, and the tabs should scroll
 * behind it.
 *
 * @param tabCount The number of tabs in the strip.
 * @param availableWidth The total width available to the strip.
 * @param itemSize The width of a single tab favicon and of the add tab button.
 * @param itemSpacing The spacing between tabs.
 * @param edgePadding The padding at each end of the strip.
 * @param expandButtonWidth The width of the pinned expand button.
 * @param showAddButton Whether the add tab button is shown.
 */
@VisibleForTesting
internal fun areTabsOverflowing(
    tabCount: Int,
    availableWidth: Dp,
    itemSize: Dp,
    itemSpacing: Dp,
    edgePadding: Dp,
    expandButtonWidth: Dp,
    showAddButton: Boolean = true,
): Boolean {
    if (tabCount == 0) return false

    val tabsWidth = itemSize * tabCount + itemSpacing * (tabCount - 1)
    val addButtonWidth = if (showAddButton) itemSpacing + itemSize else 0.dp
    val requiredWidth = edgePadding + tabsWidth + addButtonWidth
    val widthForContent = availableWidth - expandButtonWidth - edgePadding

    return requiredWidth > widthForContent
}

@Composable
private fun OverflowTrailingButtons(
    showAddButton: Boolean,
    onAddTabClick: () -> Unit,
    onExpandGroupClick: () -> Unit,
    endPadding: Dp,
    modifier: Modifier = Modifier,
) {
    Row(modifier = modifier, verticalAlignment = Alignment.CenterVertically) {
        GradientScrim()
        Row(
            modifier = Modifier.fillMaxHeight().background(MaterialTheme.colorScheme.surface).padding(end = endPadding),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            if (showAddButton) {
                AddTabButton(onClick = onAddTabClick)
            }
            ExpandButton(onClick = onExpandGroupClick)
        }
    }
}

/** The width of the gradient scrim that fades the tabs out behind the pinned trailing buttons. */
private val gradientWidth: Dp
    @Composable get() = FirefoxTheme.layout.space.static200

@Composable
private fun GradientScrim() {
    Box(
        modifier =
            Modifier.fillMaxHeight()
                .width(gradientWidth)
                .background(
                    Brush.horizontalGradient(colors = listOf(Color.Transparent, MaterialTheme.colorScheme.surface))
                )
    )
}

@Composable
private fun ExpandButton(
    onClick: () -> Unit,
    modifier: Modifier = Modifier,
) {
    IconButton(
        onClick = onClick,
        contentDescription = stringResource(R.string.tab_groups_strip_expand_group_content_description),
        modifier = modifier.testTag(TabGroupsTestTag.TAB_GROUPS_STRIP_EXPAND_BUTTON),
    ) {
        Icon(painter = painterResource(iconsR.drawable.mozac_ic_chevron_up_24), contentDescription = null)
    }
}

@Composable
private fun TabFaviconCircle(
    tab: TabGroupsStripState.Tab,
    onClick: () -> Unit,
) {
    val focusedBorder =
        if (tab.isFocused) {
            Modifier.border(FirefoxTheme.layout.border.heavy, MaterialTheme.colorScheme.primary, CircleShape)
        } else {
            Modifier
        }

    Box(
        modifier =
            Modifier.size(FirefoxTheme.layout.size.static600)
                .clip(CircleShape)
                .clickable(onClick = onClick, onClickLabel = tab.title, role = Role.Button)
                .testTag(TabGroupsTestTag.TAB_GROUPS_STRIP_TAB),
        contentAlignment = Alignment.Center,
    ) {
        Box(
            modifier =
                Modifier.size(FirefoxTheme.layout.size.static400)
                    .clip(CircleShape)
                    .background(MaterialTheme.colorScheme.surfaceContainerHighest)
                    .then(focusedBorder),
            contentAlignment = Alignment.Center,
        ) {
            Favicon(url = tab.url, size = FirefoxTheme.layout.size.static300, shape = CircleShape)
        }
    }
}

@Composable
private fun AddTabButton(onClick: () -> Unit) {
    IconButton(
        onClick = onClick,
        contentDescription = stringResource(R.string.add_tab),
        modifier = Modifier.testTag(TabGroupsTestTag.TAB_GROUPS_STRIP_ADD_BUTTON),
    ) {
        Box(
            modifier =
                Modifier.size(FirefoxTheme.layout.size.static400)
                    .clip(CircleShape)
                    .background(MaterialTheme.colorScheme.surfaceContainerHighest),
            contentAlignment = Alignment.Center,
        ) {
            Icon(painter = painterResource(iconsR.drawable.mozac_ic_plus_24), contentDescription = null)
        }
    }
}

@Composable
@Preview
private fun TabGroupsStripContentPreview(
    @PreviewParameter(TabGroupsStripStatePreviewParameterProvider::class)
    themedState: ThemedValue<TabGroupsStripState.TabGroupStripVisible>
) {
    val store = remember { TabGroupsStripStore(initialState = themedState.value) }
    val state by store.stateFlow.collectAsState()

    FirefoxTheme(theme = themedState.theme) {
        Box(modifier = Modifier.background(MaterialTheme.colorScheme.surface)) {
            TabGroupsStripContent(
                state = state as TabGroupsStripState.TabGroupStripVisible,
                listState = rememberLazyListState(),
                onTabClick = { store.dispatch(TabGroupsStripAction.TabClicked(it)) },
                onAddTabClick = { store.dispatch(TabGroupsStripAction.AddTabClicked) },
                onExpandGroupClick = { store.dispatch(TabGroupsStripAction.ExpandGroupClicked) },
                modifier = Modifier.height(FirefoxTheme.layout.size.static600),
            )
        }
    }
}

private class TabGroupsStripStatePreviewParameterProvider :
    ThemedValueProvider<TabGroupsStripState.TabGroupStripVisible>(
        baseValues = tabGroupsStripPreviewStates.map { it.second }.asSequence(),
        getDisplayName = { index, _ -> tabGroupsStripPreviewStates[index].first },
    )

private const val FILLED_STRIP_TAB_COUNT = 15

private val tabGroupsStripPreviewStates: List<Pair<String, TabGroupsStripState.TabGroupStripVisible>> =
    listOf(
        "Few tabs" to
            TabGroupsStripState.TabGroupStripVisible(
                groupId = "1",
                groupTitle = "Group",
                tabs =
                    listOf(
                        TabGroupsStripState.Tab(
                            id = "1",
                            url = "https://mozilla.org",
                            title = "Mozilla",
                            isFocused = true,
                        ),
                        TabGroupsStripState.Tab(
                            id = "2",
                            url = "https://github.com",
                            title = "GitHub",
                            isFocused = false,
                        ),
                        TabGroupsStripState.Tab(
                            id = "3",
                            url = "https://wikipedia.org",
                            title = "Wikipedia",
                            isFocused = false,
                        ),
                    ),
            ),
        "Filled strip" to
            TabGroupsStripState.TabGroupStripVisible(
                groupId = "1",
                groupTitle = "Group",
                tabs =
                    List(FILLED_STRIP_TAB_COUNT) { index ->
                        TabGroupsStripState.Tab(
                            id = index.toString(),
                            url = "https://example.com/$index",
                            title = "Tab $index",
                            isFocused = index == 0,
                        )
                    },
            ),
    )
