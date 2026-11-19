/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.tabstray.ui.tabpage

import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.tooling.preview.PreviewParameter
import androidx.compose.ui.tooling.preview.PreviewParameterProvider
import androidx.compose.ui.unit.dp
import mozilla.components.compose.base.annotation.FlexibleWindowLightDarkPreview
import mozilla.components.ui.icons.R as iconsR
import org.mozilla.fenix.R
import org.mozilla.fenix.home.collections.CollectionsMigrationPromoCard
import org.mozilla.fenix.home.collections.CollectionsMigrationPromoSource
import org.mozilla.fenix.tabstray.TabsTrayTestTag
import org.mozilla.fenix.tabstray.data.TabGroupTheme
import org.mozilla.fenix.tabstray.data.TabsTrayItem
import org.mozilla.fenix.tabstray.data.createTab
import org.mozilla.fenix.tabstray.redux.state.TabsTrayState.TabGroupState
import org.mozilla.fenix.theme.FirefoxTheme

private val EmptyPageWidth = 225.dp

/**
 * UI for displaying the Tab Groups Page in the Tab Manager.
 *
 * @param state The current snapshot of [TabGroupState].
 * @param onTabGroupClick Invoked when a group is clicked.
 * @param onEditTabGroupClick Invoked when a group is requested to be edited.
 * @param onShareTabGroupClick Invoked when a group is requested to be shared.
 * @param onDeleteTabGroupClick Invoked when a group is requested to be deleted.
 * @param onCollectionsMigrationCardDismiss Invoked when the Collections to Tab Groups migration card is dismissed.
 */
@Composable
internal fun TabGroupsPage(
    state: TabGroupState,
    onTabGroupClick: (TabsTrayItem.TabGroup) -> Unit,
    onEditTabGroupClick: (TabsTrayItem.TabGroup) -> Unit,
    onShareTabGroupClick: (TabsTrayItem.TabGroup) -> Unit,
    onDeleteTabGroupClick: (TabsTrayItem.TabGroup) -> Unit,
    onCollectionsMigrationCardDismiss: () -> Unit,
) {
    if (state.groups.isNotEmpty()) {
        Column {
            if (state.showCollectionsMigrationCard) {
                CollectionsMigrationPromoCard(
                    source = CollectionsMigrationPromoSource.TABS_TRAY,
                    modifier =
                        Modifier.padding(
                                start = FirefoxTheme.layout.space.dynamic200,
                                top = FirefoxTheme.layout.space.dynamic200,
                                end = FirefoxTheme.layout.space.dynamic200,
                            )
                            .testTag(TabsTrayTestTag.COLLECTIONS_MIGRATION_CARD),
                    onDismiss = onCollectionsMigrationCardDismiss,
                )
            }

            TabGroupList(
                groups = state.groups,
                topPadding =
                    if (state.showCollectionsMigrationCard) {
                        FirefoxTheme.layout.space.static100
                    } else {
                        FirefoxTheme.layout.space.dynamic200
                    },
                onTabGroupClick = onTabGroupClick,
                onEditTabGroupClick = onEditTabGroupClick,
                onShareTabGroupClick = onShareTabGroupClick,
                onDeleteTabGroupClick = onDeleteTabGroupClick,
            )
        }
    } else {
        EmptyTabGroupsPage()
    }
}

/**
 * UI for displaying the empty state of the Tab Groups Page in the Tab Manager.
 *
 * @param modifier The [Modifier] to be applied to the layout.
 */
@Composable
private fun EmptyTabGroupsPage(modifier: Modifier = Modifier) {
    EmptyTabPage(modifier = modifier.testTag(TabsTrayTestTag.EMPTY_TAB_GROUPS_LIST)) {
        Column(
            modifier = Modifier.width(EmptyPageWidth),
            horizontalAlignment = Alignment.CenterHorizontally,
        ) {
            Icon(
                painter = painterResource(id = iconsR.drawable.mozac_ic_tab_group_24),
                contentDescription = null,
                modifier = Modifier.size(72.dp),
                tint = MaterialTheme.colorScheme.surfaceContainerHighest,
            )

            Spacer(modifier = Modifier.height(8.dp))

            Text(
                text = stringResource(id = R.string.tab_manager_empty_tab_groups_page_header),
                textAlign = TextAlign.Center,
                style = FirefoxTheme.typography.headline7,
            )

            Spacer(modifier = Modifier.height(4.dp))

            Text(
                text = stringResource(id = R.string.tab_manager_empty_tab_groups_page_description),
                textAlign = TextAlign.Center,
                style = FirefoxTheme.typography.caption,
            )
        }
    }
}

private class TabGroupsPagePreviewParameterProvider : PreviewParameterProvider<List<TabsTrayItem.TabGroup>> {
    val data =
        listOf(
            Pair(
                "Empty",
                emptyList(),
            ),
            Pair(
                "2 Tab Groups",
                listOf(
                    TabsTrayItem.TabGroup(
                        title = "Work",
                        theme = TabGroupTheme.Blue,
                        tabs =
                            mutableListOf(
                                createTab(url = "https://www.mozilla.org"),
                                createTab(url = "https://www.firefox.com"),
                            ),
                    ),
                    TabsTrayItem.TabGroup(
                        title = "Other Work",
                        theme = TabGroupTheme.Purple,
                        tabs =
                            mutableListOf(
                                createTab(url = "https://www.mozilla.org"),
                                createTab(url = "https://www.firefox.com"),
                                createTab(url = "https://www.mozilla.org/about"),
                            ),
                    ),
                ),
            ),
        )
    override val values: Sequence<List<TabsTrayItem.TabGroup>>
        get() = data.map { it.second }.asSequence()

    override fun getDisplayName(index: Int): String {
        return data[index].first
    }
}

@FlexibleWindowLightDarkPreview
@Composable
private fun TabGroupsPagePreview(
    @PreviewParameter(TabGroupsPagePreviewParameterProvider::class) groups: List<TabsTrayItem.TabGroup>
) {
    FirefoxTheme {
        TabGroupsPage(
            state = TabGroupState(groups = groups),
            onTabGroupClick = {},
            onEditTabGroupClick = {},
            onShareTabGroupClick = {},
            onDeleteTabGroupClick = {},
            onCollectionsMigrationCardDismiss = {},
        )
    }
}

@FlexibleWindowLightDarkPreview
@Composable
private fun TabGroupsPageWithCollectionsMigrationCardPreview() {
    FirefoxTheme {
        TabGroupsPage(
            state =
                TabGroupState(
                    groups =
                        listOf(
                            TabsTrayItem.TabGroup(
                                title = "Work",
                                theme = TabGroupTheme.Blue,
                                tabs = mutableListOf(createTab(url = "https://www.mozilla.org")),
                            )
                        ),
                    showCollectionsMigrationCard = true,
                ),
            onTabGroupClick = {},
            onEditTabGroupClick = {},
            onShareTabGroupClick = {},
            onDeleteTabGroupClick = {},
            onCollectionsMigrationCardDismiss = {},
        )
    }
}
