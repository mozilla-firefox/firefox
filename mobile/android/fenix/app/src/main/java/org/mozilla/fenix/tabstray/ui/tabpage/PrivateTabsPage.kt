/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.tabstray.ui.tabpage

import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.width
import androidx.compose.material3.Icon
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import mozilla.components.compose.base.annotation.FlexibleWindowPreview
import mozilla.components.compose.base.theme.Theme
import mozilla.components.compose.draggable.ItemInteractionHandler
import mozilla.components.ui.icons.R as iconsR
import org.mozilla.fenix.R
import org.mozilla.fenix.pbmlock.UnlockPrivateTabsTrayScreen
import org.mozilla.fenix.tabstray.TabsTrayTestTag
import org.mozilla.fenix.tabstray.data.TabsTrayItem
import org.mozilla.fenix.tabstray.redux.state.TabsTrayState.Mode
import org.mozilla.fenix.tabstray.redux.state.TabsTrayState.PrivateBrowsingState
import org.mozilla.fenix.tabstray.redux.state.TabsTrayState.TabsTrayConfig
import org.mozilla.fenix.tabstray.telemetry.OnTabClose
import org.mozilla.fenix.tabstray.telemetry.TabCloseType
import org.mozilla.fenix.theme.FirefoxTheme

private val EmptyPageWidth = 190.dp

/**
 * UI for displaying the Private Tabs Page in the Tab Manager.
 *
 * @param state The current snapshot of [PrivateBrowsingState].
 * @param config The current snapshot of [TabsTrayConfig].
 * @param onTabClose Invoked when the user closes a tab, with the [TabCloseType] of the interaction.
 * @param onItemClick Invoked when the user clicks on a tab.
 * @param onItemLongClick Invoked when the user long clicks on a tab.
 * @param itemInteractionHandler Handles tab interactions such as moves and drag and drop.
 * @param onUnlockPbmClick Invoked when user clicks on Unlock button.
 */
@Suppress("LongParameterList")
@Composable
internal fun PrivateTabsPage(
    state: PrivateBrowsingState,
    config: TabsTrayConfig,
    onTabClose: OnTabClose,
    onItemClick: (TabsTrayItem) -> Unit,
    onItemLongClick: (TabsTrayItem) -> Unit,
    itemInteractionHandler: ItemInteractionHandler,
    onUnlockPbmClick: () -> Unit,
) {
    when {
        state.tabs.isEmpty() -> {
            EmptyPrivateTabsPage()
        }

        state.isLocked -> {
            UnlockPrivateTabsTrayScreen { onUnlockPbmClick() }
        }

        else -> {
            TabLayout(
                tabs = state.tabs,
                displayTabsInGrid = config.displayTabsInGrid,
                itemInteractionHandler = itemInteractionHandler,
                selectedItemIndex = state.selectedItemIndex,
                selectionMode = Mode.Normal, // Multiselection is not supported in private tabs
                modifier = Modifier.testTag(TabsTrayTestTag.PRIVATE_TABS_LIST),
                onTabClose = onTabClose,
                onItemClick = onItemClick,
                onItemLongClick = onItemLongClick,
                onEditTabGroupClick = {},
                onCloseTabGroupClick = {},
                onShareTabGroupClick = {},
                onDeleteTabGroupClick = {},
                onUngroupTabGroupClick = {},
                onTabGroupOnboardingDismiss = {},
                dragAndDropEnabled = false,
                displayTabGroupOnboarding = false,
                focusEnabled = true, // Drag and drop is not possible, so there's no reason to hide the focus state
                liveReorderEnabled = true, // Technically, trivially true as it uses ReorderableGrid today
            )
        }
    }
}

/**
 * UI for displaying the empty state of the Private Tabs Page in the Tab Manager.
 *
 * @param modifier The [Modifier] to be applied to the layout.
 */
@Composable
private fun EmptyPrivateTabsPage(modifier: Modifier = Modifier) {
    EmptyTabPage(modifier = modifier.testTag(TabsTrayTestTag.EMPTY_PRIVATE_TABS_LIST)) {
        Column(
            modifier = Modifier.width(EmptyPageWidth),
            horizontalAlignment = Alignment.CenterHorizontally,
        ) {
            Icon(
                painter = painterResource(id = iconsR.drawable.mozac_ic_private_mode_fill_72),
                contentDescription = null,
            )

            Text(
                text = stringResource(id = R.string.tab_manager_empty_private_tabs_page_header),
                textAlign = TextAlign.Center,
                style = FirefoxTheme.typography.headline7,
            )

            Spacer(modifier = Modifier.height(4.dp))

            Text(
                text =
                    stringResource(
                        id = R.string.tab_manager_empty_private_tabs_page_description,
                        stringResource(id = R.string.app_name),
                    ),
                textAlign = TextAlign.Center,
                style = FirefoxTheme.typography.caption,
            )
        }
    }
}

@FlexibleWindowPreview
@Composable
private fun EmptyPrivateTabsPagePreview() {
    FirefoxTheme(theme = Theme.Private) {
        Surface {
            EmptyPrivateTabsPage()
        }
    }
}
