/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.tabgroups

import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.wrapContentHeight
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.res.pluralStringResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.clearAndSetSemantics
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.heading
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.tooling.preview.PreviewParameter
import androidx.compose.ui.tooling.preview.PreviewParameterProvider
import androidx.compose.ui.unit.dp
import mozilla.components.compose.base.annotation.FlexibleWindowLightDarkPreview
import mozilla.components.compose.base.button.IconButton
import mozilla.components.compose.draggable.ItemInteractionHandler
import mozilla.components.compose.draggable.NoOpItemInteractionHandler
import mozilla.components.ui.icons.R as iconsR
import org.mozilla.fenix.R
import org.mozilla.fenix.tabstray.TabsTrayTestTag
import org.mozilla.fenix.tabstray.data.TabGroupTheme
import org.mozilla.fenix.tabstray.data.TabsTrayItem
import org.mozilla.fenix.tabstray.data.createTab
import org.mozilla.fenix.tabstray.data.createTabGroup
import org.mozilla.fenix.tabstray.redux.state.TabsTrayState
import org.mozilla.fenix.tabstray.telemetry.OnTabClose
import org.mozilla.fenix.tabstray.telemetry.TabCloseType
import org.mozilla.fenix.tabstray.ui.tabitems.LOREM_IPSUM
import org.mozilla.fenix.tabstray.ui.tabitems.TabGroupMenuButton
import org.mozilla.fenix.tabstray.ui.tabpage.TabLayout
import org.mozilla.fenix.theme.FirefoxTheme

/**
 * Renders an expanded view of a user's tab group.
 *
 * @param group [TabsTrayItem.TabGroup] item rendered by the card.
 * @param actions [ExpandedTabGroupActions] invoked in response to user interactions.
 * @param displayTabsInGrid Whether the group's tabs are displayed in a grid (vs a list).
 * @param itemInteractionHandler Handler for tab interactions.
 */
@Composable
fun ExpandedTabGroup(
    group: TabsTrayItem.TabGroup,
    actions: ExpandedTabGroupActions,
    displayTabsInGrid: Boolean,
    itemInteractionHandler: ItemInteractionHandler,
) {
    Column(
        modifier =
            Modifier.testTag(TabsTrayTestTag.TAB_GROUP_BOTTOM_SHEET_ROOT)
                .padding(
                    start = FirefoxTheme.layout.space.dynamic200,
                    end = FirefoxTheme.layout.space.dynamic200,
                )
    ) {
        ViewTabGroupHeader(
            title = group.title,
            groupTheme = group.theme,
            groupTabsSize = group.tabs.size,
            actions = actions,
        )

        TabLayout(
            tabs = group.tabs,
            displayTabsInGrid = displayTabsInGrid,
            dragAndDropEnabled = false,
            reorderingEnabled = true,
            displayTabGroupOnboarding = false,
            liveReorderEnabled = true,
            selectedItemIndex = group.initialScrollIndex,
            selectionMode = TabsTrayState.Mode.Normal,
            itemInteractionHandler = itemInteractionHandler,
            modifier = Modifier,
            onTabClose = actions.onTabClose,
            onItemClick = actions.onItemClick,
            onItemLongClick = { item -> }, // Ignore long click
            onEditTabGroupClick = {}, // Ignore tab group edits
            onCloseTabGroupClick = {}, // Ignore tab group closes
            onShareTabGroupClick = {}, // Ignore tab group shares
            onDeleteTabGroupClick = {}, // Ignore tab group deletes
            onUngroupTabGroupClick = {}, // Ignore tab group ungroups
            onTabGroupOnboardingDismiss = {}, // Ignore onboarding dismissals - onboarding is not shown in this layout
            contentPadding = PaddingValues(0.dp), // TabLayout should not have its own content padding inside this view
            listHorizontalPadding = 0.dp, // The list layout should not add its own horizontal padding inside this view
            focusEnabled = true, // Drag and drop is not possible in this view, so focus should never be suppressed
        )
    }
}

@Composable
private fun ViewTabGroupHeader(
    title: String,
    groupTabsSize: Int,
    groupTheme: TabGroupTheme,
    actions: ExpandedTabGroupActions,
) {
    Row(
        modifier =
            Modifier.fillMaxWidth()
                .padding(
                    top = FirefoxTheme.layout.space.static150,
                    bottom = FirefoxTheme.layout.space.static200,
                )
                .wrapContentHeight(),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        val headerContentDescription =
            pluralStringResource(
                id = R.plurals.expanded_tab_group_header_description,
                count = groupTabsSize,
                title,
                groupTabsSize,
                groupTheme.contentLabel,
            )

        Row(
            modifier =
                Modifier.weight(1f).semantics(mergeDescendants = true) {
                    heading()
                    contentDescription = headerContentDescription
                },
            verticalAlignment = Alignment.CenterVertically,
        ) {
            TabGroupThemeDot(groupTheme)

            Spacer(modifier = Modifier.width(FirefoxTheme.layout.space.static100))

            Text(
                text = title,
                modifier = Modifier.weight(1f).clearAndSetSemantics {},
                color = MaterialTheme.colorScheme.onSurface,
                maxLines = 1,
                overflow = TextOverflow.Ellipsis,
                style = FirefoxTheme.typography.headline7,
            )
        }

        Spacer(modifier = Modifier.width(FirefoxTheme.layout.space.static200 + FirefoxTheme.layout.space.static25))

        val onAddNewTabClick = actions.onAddNewTabClick
        if (onAddNewTabClick != null) {
            AddTabToGroupButton(onClick = onAddNewTabClick)

            Spacer(modifier = Modifier.width(FirefoxTheme.layout.space.static100))
        }

        TabGroupMenuButton(
            includeCloseOption = true,
            includeUngroupOption = true,
            onDeleteTabGroupClick = actions.onDeleteTabGroupClick,
            onEditTabGroupClick = actions.onEditTabGroupClick,
            onCloseTabGroupClick = actions.onCloseTabGroupClick,
            onShareTabGroupClick = actions.onShareTabGroupClick,
            onUngroupTabGroupClick = actions.onUngroupTabGroupClick,
        )
    }
}

@Composable
private fun AddTabToGroupButton(
    onClick: () -> Unit,
    modifier: Modifier = Modifier,
) {
    IconButton(
        onClick = onClick,
        contentDescription = stringResource(id = R.string.add_tab),
        modifier = modifier.testTag(TabsTrayTestTag.BOTTOM_SHEET_ADD_TAB_BUTTON),
    ) {
        Icon(
            painter = painterResource(id = iconsR.drawable.mozac_ic_plus_24),
            contentDescription = null,
            tint = MaterialTheme.colorScheme.onSurface,
        )
    }
}

@FlexibleWindowLightDarkPreview
@Composable
private fun ExpandedTabGroupPreview(
    @PreviewParameter(ExpandedTabGroupPreviewProvider::class) previewState: ExpandedTabGroupPreviewState
) {
    FirefoxTheme {
        Surface {
            ExpandedTabGroup(
                group = previewState.group,
                actions =
                    ExpandedTabGroupActions(
                        onItemClick = {},
                        onTabClose = { _, _ -> },
                        onDeleteTabGroupClick = {},
                        onEditTabGroupClick = {},
                        onCloseTabGroupClick = {},
                        onUngroupTabGroupClick = {},
                        onAddNewTabClick = {},
                        onShareTabGroupClick = {},
                    ),
                displayTabsInGrid = previewState.displayTabsInGrid,
                itemInteractionHandler = NoOpItemInteractionHandler,
            )
        }
    }
}

private fun generateFakeTabsList(tabCount: Int = 10): MutableList<TabsTrayItem.Tab> =
    MutableList(tabCount) { index ->
        createTab(
            id = "tab$index",
            title = "Tab $index",
            url = "www.mozilla.com",
            private = false,
        )
    }

private data class ExpandedTabGroupPreviewState(
    val group: TabsTrayItem.TabGroup,
    val selectedTabId: String? = null,
    val displayTabsInGrid: Boolean = true,
)

private class ExpandedTabGroupPreviewProvider : PreviewParameterProvider<ExpandedTabGroupPreviewState> {
    val data =
        listOf(
            Pair(
                "1 Tab",
                ExpandedTabGroupPreviewState(
                    group =
                        createTabGroup(
                            title = "Tab Group",
                            tabs = generateFakeTabsList(tabCount = 1),
                        )
                ),
            ),
            Pair(
                "2 Tabs",
                ExpandedTabGroupPreviewState(
                    group =
                        createTabGroup(
                            title = "Tab Group",
                            tabs = generateFakeTabsList(tabCount = 2),
                        )
                ),
            ),
            Pair(
                "3 Tabs",
                ExpandedTabGroupPreviewState(
                    group =
                        createTabGroup(
                            title = "Tab Group",
                            tabs = generateFakeTabsList(tabCount = 3),
                        )
                ),
            ),
            Pair(
                "4 Tabs",
                ExpandedTabGroupPreviewState(
                    group =
                        createTabGroup(
                            title = "Tab Group",
                            tabs = generateFakeTabsList(),
                        )
                ),
            ),
            Pair(
                "Selected tab",
                ExpandedTabGroupPreviewState(
                    group =
                        createTabGroup(
                            title = "Tab Group",
                            tabs = generateFakeTabsList(),
                        ),
                    selectedTabId = "tabid0",
                ),
            ),
            Pair(
                "Large title",
                ExpandedTabGroupPreviewState(
                    group =
                        createTabGroup(
                            title = LOREM_IPSUM,
                            tabs = generateFakeTabsList(),
                        ),
                    selectedTabId = "tabid0",
                ),
            ),
            Pair(
                "List view",
                ExpandedTabGroupPreviewState(
                    group =
                        createTabGroup(
                            title = "Tab Group",
                            tabs = generateFakeTabsList(),
                        ),
                    displayTabsInGrid = false,
                ),
            ),
        )
    override val values: Sequence<ExpandedTabGroupPreviewState>
        get() = data.map { it.second }.asSequence()

    override fun getDisplayName(index: Int): String {
        return data[index].first
    }
}

/**
 * User interactions handled by the [ExpandedTabGroup] view.
 *
 * @property onItemClick Invoked when the user clicks on a [TabsTrayItem] in the group.
 * @property onTabClose Invoked when the user closes a [TabsTrayItem.Tab] in the group, with the [TabCloseType] of the
 *   interaction.
 * @property onDeleteTabGroupClick Invoked when the user clicks on delete tab group.
 * @property onEditTabGroupClick Invoked when the user clicks to edit the group.
 * @property onCloseTabGroupClick Invoked when the user clicks to close a tab group.
 * @property onUngroupTabGroupClick Invoked when the user clicks to ungroup a tab group.
 * @property onAddNewTabClick Invoked when the user clicks to add a new tab to the group. When null, the add-tab button
 *   is hidden.
 * @property onShareTabGroupClick Invoked when the user clicks to share the group.
 */
data class ExpandedTabGroupActions(
    val onItemClick: (TabsTrayItem) -> Unit,
    val onTabClose: OnTabClose,
    val onDeleteTabGroupClick: () -> Unit,
    val onEditTabGroupClick: () -> Unit,
    val onCloseTabGroupClick: () -> Unit,
    val onUngroupTabGroupClick: () -> Unit,
    val onAddNewTabClick: (() -> Unit)?,
    val onShareTabGroupClick: () -> Unit,
)
