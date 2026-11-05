/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.tabgroups

import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.res.dimensionResource
import androidx.compose.ui.tooling.preview.PreviewLightDark
import org.mozilla.fenix.R
import org.mozilla.fenix.tabgroups.strip.TabGroupsStripContent
import org.mozilla.fenix.tabgroups.strip.redux.TabGroupsStripAction
import org.mozilla.fenix.tabgroups.strip.redux.TabGroupsStripState
import org.mozilla.fenix.tabgroups.strip.redux.TabGroupsStripStore
import org.mozilla.fenix.theme.FirefoxTheme

/**
 * The bottom toolbar shown while the active tab is in a group.
 *
 * @param store The [TabGroupsStripStore] driving the strip.
 * @param modifier The [Modifier] to be applied to the strip.
 * @param containerColor The background color.
 */
@Composable
fun TabGroupsStrip(
    store: TabGroupsStripStore,
    modifier: Modifier = Modifier,
    containerColor: Color = MaterialTheme.colorScheme.surface,
) {
    val state by store.stateFlow.collectAsState()
    val visibleStrip = state as? TabGroupsStripState.TabGroupStripVisible ?: return

    Surface(
        modifier = modifier.fillMaxWidth().height(dimensionResource(R.dimen.tab_groups_strip_height)),
        color = containerColor,
    ) {
        TabGroupsStripContent(
            state = visibleStrip,
            onTabClick = { store.dispatch(TabGroupsStripAction.TabClicked(it)) },
            onAddTabClick = { store.dispatch(TabGroupsStripAction.AddTabClicked) },
            onExpandGroupClick = { store.dispatch(TabGroupsStripAction.ExpandGroupClicked) },
        )
    }
}

@PreviewLightDark
@Composable
private fun TabGroupsStripPreview() {
    val store = remember {
        TabGroupsStripStore(
            initialState =
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
                            )
                        ),
                )
        )
    }

    FirefoxTheme {
        TabGroupsStrip(store = store)
    }
}

@PreviewLightDark
@Composable
private fun TabGroupsStripHiddenPreview() {
    val store = remember { TabGroupsStripStore(initialState = TabGroupsStripState.TabGroupStripHidden) }

    FirefoxTheme {
        TabGroupsStrip(store = store)
    }
}
