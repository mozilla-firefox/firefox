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
import androidx.compose.foundation.lazy.LazyRow
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.runtime.Composable
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
 * A horizontal strip of the active group's tabs, each shown as a favicon circle, followed by an add tab button and an
 * expand button.
 *
 * @param state The [TabGroupsStripState] to render.
 * @param onTabClick Invoked with a tab's ID when its favicon is tapped.
 * @param onAddTabClick Invoked when the add tab button is tapped.
 * @param onExpandGroupClick Invoked when the expand button is tapped.
 * @param modifier The [Modifier] to be applied to the strip.
 */
@Composable
internal fun TabGroupsStripContent(
    state: TabGroupsStripState.TabGroupStripVisible,
    onTabClick: (String) -> Unit,
    onAddTabClick: () -> Unit,
    onExpandGroupClick: () -> Unit,
    modifier: Modifier = Modifier,
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
            remember(state.tabs.size, maxWidth) {
                areTabsOverflowing(
                    tabCount = state.tabs.size,
                    availableWidth = maxWidth,
                    itemSize = itemSize,
                    itemSpacing = itemSpacing,
                    edgePadding = edgePadding,
                    expandButtonWidth = expandButtonWidth,
                )
            }

        // The tabs live in their own LazyRow so screen readers announce them as a list.
        // The add and expand buttons are kept outside it so they read as "out of list".
        val tabItems: LazyListScope.() -> Unit = {
            items(items = state.tabs, key = { it.id }) { tab ->
                TabFaviconCircle(tab = tab, onClick = { onTabClick(tab.id) })
            }
        }

        if (tabsOverflowing) {
            OverflowingTabsStrip(
                tabItems = tabItems,
                onAddTabClick = onAddTabClick,
                onExpandGroupClick = onExpandGroupClick,
                itemSpacing = itemSpacing,
                itemSize = itemSize,
                edgePadding = edgePadding,
                expandButtonWidth = expandButtonWidth,
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
                    content = tabItems,
                )
                AddTabButton(onClick = onAddTabClick)
            }

            ExpandButton(
                onClick = onExpandGroupClick,
                modifier = Modifier.align(Alignment.CenterEnd).padding(end = edgePadding),
            )
        }
    }
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
 */
@VisibleForTesting
internal fun areTabsOverflowing(
    tabCount: Int,
    availableWidth: Dp,
    itemSize: Dp,
    itemSpacing: Dp,
    edgePadding: Dp,
    expandButtonWidth: Dp,
): Boolean {
    if (tabCount == 0) return false

    val addButtonWidth = itemSize
    val tabsWidth = itemSize * tabCount + itemSpacing * (tabCount - 1)
    val requiredWidth = edgePadding + tabsWidth + itemSpacing + addButtonWidth
    val widthForContent = availableWidth - expandButtonWidth - edgePadding

    return requiredWidth > widthForContent
}

@Composable
private fun BoxScope.OverflowingTabsStrip(
    tabItems: LazyListScope.() -> Unit,
    onAddTabClick: () -> Unit,
    onExpandGroupClick: () -> Unit,
    itemSpacing: Dp,
    itemSize: Dp,
    edgePadding: Dp,
    expandButtonWidth: Dp,
) {
    LazyRow(
        modifier = Modifier.fillMaxWidth(),
        horizontalArrangement = Arrangement.spacedBy(itemSpacing),
        contentPadding =
            PaddingValues(
                start = edgePadding,
                end = gradientWidth + itemSize + expandButtonWidth + edgePadding,
            ),
        verticalAlignment = Alignment.CenterVertically,
        content = tabItems,
    )

    OverflowTrailingButtons(
        onAddTabClick = onAddTabClick,
        onExpandGroupClick = onExpandGroupClick,
        endPadding = edgePadding,
        modifier = Modifier.align(Alignment.CenterEnd),
    )
}

@Composable
private fun OverflowTrailingButtons(
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
            AddTabButton(onClick = onAddTabClick)
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
