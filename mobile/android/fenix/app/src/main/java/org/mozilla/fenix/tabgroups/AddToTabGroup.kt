/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.tabgroups

import androidx.compose.foundation.background
import androidx.compose.foundation.combinedClickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.defaultMinSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.ModalBottomSheet
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.clearAndSetSemantics
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.role
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.tooling.preview.Preview
import androidx.compose.ui.tooling.preview.PreviewParameter
import androidx.compose.ui.tooling.preview.PreviewParameterProvider
import androidx.compose.ui.unit.dp
import mozilla.components.browser.state.state.createTab
import mozilla.components.compose.base.annotation.FlexibleWindowPreview
import mozilla.components.compose.base.theme.PreviewThemeProvider
import mozilla.components.compose.base.theme.Theme
import mozilla.components.ui.icons.R as iconsR
import org.mozilla.fenix.R
import org.mozilla.fenix.compose.rememberModalSheetState
import org.mozilla.fenix.tabstray.TabsTrayTestTag
import org.mozilla.fenix.tabstray.data.TabGroupTheme
import org.mozilla.fenix.tabstray.data.TabsTrayItem
import org.mozilla.fenix.theme.FirefoxTheme

private val NEW_TAB_GROUP_COMPONENT_HEIGHT = 40.dp
private val NEW_TAB_GROUP_COMPONENT_WIDTH = 78.dp

/**
 * Prompt for the user to choose whether to add to a new or an existing tab group.
 *
 * @param tabGroups List of existing Tab Groups.
 * @param onAddToNewTabGroup Invoked when user clicks to add to a new tab group.
 * @param onAddToExistingTabGroup Invoked when user clicks to add to an existing tab group.
 */
@Composable
fun AddToTabGroup(
    tabGroups: List<TabsTrayItem.TabGroup>,
    onAddToNewTabGroup: () -> Unit,
    onAddToExistingTabGroup: (TabsTrayItem.TabGroup) -> Unit,
) {
    AddToTabGroupContent(
        tabGroups = tabGroups,
        onAddToNewTabGroup = onAddToNewTabGroup,
        onAddToExistingTabGroup = onAddToExistingTabGroup,
    )
}

@Composable
private fun AddToTabGroupContent(
    tabGroups: List<TabsTrayItem.TabGroup>,
    onAddToNewTabGroup: () -> Unit,
    onAddToExistingTabGroup: (TabsTrayItem.TabGroup) -> Unit,
) {
    Column(modifier = Modifier.fillMaxWidth().testTag(TabsTrayTestTag.ADD_TO_TAB_GROUP_ROOT)) {
        Text(
            text = stringResource(R.string.add_to_tab_group_title),
            modifier =
                Modifier.fillMaxWidth()
                    .padding(
                        horizontal = FirefoxTheme.layout.space.dynamic200,
                        vertical = FirefoxTheme.layout.space.static150,
                    ),
            style = FirefoxTheme.typography.headline7,
        )

        LazyColumn(
            modifier = Modifier.fillMaxWidth(),
            contentPadding = PaddingValues(bottom = 12.dp),
            verticalArrangement = Arrangement.spacedBy(FirefoxTheme.layout.space.static200),
        ) {
            item {
                NewTabGroupContent(
                    onClick = onAddToNewTabGroup,
                    modifier = Modifier.fillMaxWidth(),
                )
            }

            items(
                items = tabGroups,
                key = { it.id },
            ) { tabGroup ->
                TabGroupRow(
                    tabGroup = tabGroup,
                    onClick = { onAddToExistingTabGroup(tabGroup) },
                    modifier = Modifier.fillMaxWidth(),
                )
            }
        }
    }
}

@Composable
private fun NewTabGroupContent(
    onClick: () -> Unit,
    modifier: Modifier = Modifier,
) {
    val newTabGroupContentDescription = stringResource(id = R.string.add_to_new_tab_group_content_description)
    Row(
        modifier =
            modifier
                .testTag(TabsTrayTestTag.ADD_TO_NEW_TAB_GROUP)
                .defaultMinSize(minHeight = NEW_TAB_GROUP_COMPONENT_HEIGHT)
                .padding(horizontal = FirefoxTheme.layout.space.dynamic200)
                .semantics(mergeDescendants = true) {
                    contentDescription = newTabGroupContentDescription
                    role = Role.Button
                }
                .combinedClickable(onClick = onClick),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(FirefoxTheme.layout.space.static200),
    ) {
        Box(
            modifier =
                Modifier.size(width = NEW_TAB_GROUP_COMPONENT_WIDTH, height = NEW_TAB_GROUP_COMPONENT_HEIGHT)
                    .background(
                        MaterialTheme.colorScheme.surfaceContainerHighest,
                        shape = MaterialTheme.shapes.extraSmall,
                    ),
            contentAlignment = Alignment.Center,
        ) {
            Icon(
                painter = painterResource(iconsR.drawable.mozac_ic_plus_24),
                contentDescription = null,
                modifier = Modifier.padding(vertical = 8.dp, horizontal = 27.dp),
                tint = MaterialTheme.colorScheme.secondary,
            )
        }

        Text(
            text = stringResource(R.string.add_to_new_tab_group_title),
            modifier = Modifier.weight(1f).clearAndSetSemantics {},
            style = FirefoxTheme.typography.body1,
            maxLines = 1,
            overflow = TextOverflow.Ellipsis,
        )
    }
}

private class TabGroupsParameterProvider : PreviewParameterProvider<List<TabsTrayItem.TabGroup>> {
    val tab1 = TabsTrayItem.Tab(createTab("test1"))
    val tab2 = TabsTrayItem.Tab(createTab("test2"))
    val tab3 = TabsTrayItem.Tab(createTab("test3"))
    val tab4 = TabsTrayItem.Tab(createTab("test4"))

    val data =
        listOf(
            Pair(
                "No existing tab groups",
                emptyList(),
            ),
            Pair(
                "Two existing tab groups",
                listOf(
                    TabsTrayItem.TabGroup(
                        id = "1",
                        title = "Group 1",
                        theme = TabGroupTheme.default,
                        tabs = mutableListOf(tab1),
                    ),
                    TabsTrayItem.TabGroup(
                        id = "2",
                        title = "Group 2",
                        theme = TabGroupTheme.default,
                        tabs = mutableListOf(tab1, tab2),
                    ),
                    TabsTrayItem.TabGroup(
                        id = "3",
                        title = "Group 3",
                        theme = TabGroupTheme.default,
                        tabs = mutableListOf(tab1, tab2, tab3, tab4),
                    ),
                ),
            ),
        )

    override fun getDisplayName(index: Int): String {
        return data[index].first
    }

    override val values: Sequence<List<TabsTrayItem.TabGroup>>
        get() = data.map { it.second }.asSequence()
}

@Preview
@Composable
private fun AddToTabGroupContentPreview(
    @PreviewParameter(TabGroupsParameterProvider::class) tabGroups: List<TabsTrayItem.TabGroup>
) {
    FirefoxTheme {
        Surface {
            AddToTabGroupContent(
                tabGroups = tabGroups,
                onAddToNewTabGroup = {},
                onAddToExistingTabGroup = {},
            )
        }
    }
}

@OptIn(ExperimentalMaterial3Api::class)
@FlexibleWindowPreview
@Composable
private fun AddToTabGroupBottomSheetPreview(@PreviewParameter(PreviewThemeProvider::class) theme: Theme) {
    val sheetState = rememberModalSheetState()
    LaunchedEffect(Unit) {
        if (!sheetState.isVisible) {
            sheetState.show()
        }
    }

    FirefoxTheme(theme) {
        Surface {
            ModalBottomSheet(
                sheetState = sheetState,
                onDismissRequest = {},
            ) {
                AddToTabGroupContent(
                    tabGroups = emptyList(),
                    onAddToNewTabGroup = {},
                    onAddToExistingTabGroup = {},
                )
            }
        }
    }
}
