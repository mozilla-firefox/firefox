/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.tabstray.ui.tabpage

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.derivedStateOf
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalResources
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import mozilla.components.compose.base.annotation.FlexibleWindowLightDarkPreview
import org.mozilla.fenix.R
import org.mozilla.fenix.tabstray.TabsTrayTestTag
import org.mozilla.fenix.tabstray.data.TabsTrayItem
import org.mozilla.fenix.tabstray.redux.state.TabsTrayState
import org.mozilla.fenix.tabstray.ui.inactivetabs.InactiveTabsList
import org.mozilla.fenix.theme.FirefoxTheme
import mozilla.components.ui.icons.R as iconsR

private val EmptyPageWidth = 170.dp

/**
 * UI for displaying the Normal Tabs Page in the Tab Manager.
 *
 * @param normalTabs The list of active tabs to display.
 * @param inactiveTabs The list of inactive tabs to display.
 * @param selectedTabId The ID of the currently selected tab.
 * @param selectionMode [TabsTrayState.Mode] indicating whether the Tab Manager is in single selection.
 * @param inactiveTabsExpanded Whether the Inactive Tabs section is expanded.
 * @param displayTabsInGrid Whether the normal and private tabs should be displayed in a grid.
 * @param onTabClose Invoked when the user clicks to close a tab.
 * @param onItemClick Invoked when the user clicks on a tab.
 * @param onItemLongClick Invoked when the user long clicks a tab.
 * @param shouldShowInactiveTabsAutoCloseDialog Whether the inactive tabs auto close dialog should be displayed.
 * @param onInactiveTabsHeaderClick Invoked when the user clicks on the inactive tabs section header.
 * @param onDeleteAllInactiveTabsClick Invoked when the user clicks on the delete all inactive tabs button.
 * @param onInactiveTabsAutoCloseDialogShown Invoked when the inactive tabs auto close dialog
 * is presented to the user.
 * @param onInactiveTabAutoCloseDialogCloseButtonClick Invoked when the user clicks on the inactive
 * tab auto close dialog's dismiss button.
 * @param onEnableInactiveTabAutoCloseClick Invoked when the user clicks on the inactive tab auto
 * close dialog's enable button.
 * @param onInactiveTabClick Invoked when the user clicks on an inactive tab.
 * @param onInactiveTabClose Invoked when the user clicks on an inactive tab's close button.
 * @param onMove Invoked after the drag and drop gesture completed. Swaps position of two tabs.
 * @param shouldShowInactiveTabsCFR Returns whether the inactive tabs CFR is displayed.
 * @param onInactiveTabsCFRShown Invoked when the inactive tabs CFR is displayed.
 * @param onInactiveTabsCFRClick Invoked when the inactive tabs CFR is clicked.
 * @param onInactiveTabsCFRDismiss Invoked when the inactive tabs CFR is dismissed.
 * @param onTabDragStart Invoked when a tab drag has been started.
 */
@Composable
@Suppress("LongParameterList")
internal fun NormalTabsPage(
    normalTabs: List<TabsTrayItem>,
    inactiveTabs: List<TabsTrayItem.Tab>,
    selectedTabId: String?,
    selectionMode: TabsTrayState.Mode,
    inactiveTabsExpanded: Boolean,
    displayTabsInGrid: Boolean,
    onTabClose: (TabsTrayItem.Tab) -> Unit,
    onItemClick: (TabsTrayItem) -> Unit,
    onItemLongClick: (TabsTrayItem) -> Unit,
    shouldShowInactiveTabsAutoCloseDialog: (Int) -> Boolean,
    onInactiveTabsHeaderClick: (Boolean) -> Unit,
    onDeleteAllInactiveTabsClick: () -> Unit,
    onInactiveTabsAutoCloseDialogShown: () -> Unit,
    onInactiveTabAutoCloseDialogCloseButtonClick: () -> Unit,
    onEnableInactiveTabAutoCloseClick: () -> Unit,
    onInactiveTabClick: (TabsTrayItem.Tab) -> Unit,
    onInactiveTabClose: (TabsTrayItem.Tab) -> Unit,
    onMove: (String, String?, Boolean) -> Unit,
    shouldShowInactiveTabsCFR: () -> Boolean,
    onInactiveTabsCFRShown: () -> Unit,
    onInactiveTabsCFRClick: () -> Unit,
    onInactiveTabsCFRDismiss: () -> Unit,
    onTabDragStart: () -> Unit,
) {
    if (normalTabs.isNotEmpty() || inactiveTabs.isNotEmpty()) {
        val showInactiveTabsAutoCloseDialog by remember(inactiveTabs) {
            derivedStateOf {
                shouldShowInactiveTabsAutoCloseDialog(inactiveTabs.size)
            }
        }

        var showAutoCloseDialog by remember { mutableStateOf(showInactiveTabsAutoCloseDialog) }

        val optionalInactiveTabsHeader: (@Composable () -> Unit)? = if (inactiveTabs.isEmpty()) {
            null
        } else {
            {
                InactiveTabsList(
                    inactiveTabs = inactiveTabs,
                    expanded = inactiveTabsExpanded,
                    showAutoCloseDialog = showAutoCloseDialog,
                    showCFR = shouldShowInactiveTabsCFR(),
                    onHeaderClick = onInactiveTabsHeaderClick,
                    onDeleteAllButtonClick = onDeleteAllInactiveTabsClick,
                    onAutoCloseDismissClick = {
                        onInactiveTabAutoCloseDialogCloseButtonClick()
                        showAutoCloseDialog = !showAutoCloseDialog
                    },
                    onEnableAutoCloseClick = {
                        onEnableInactiveTabAutoCloseClick()
                        showAutoCloseDialog = !showAutoCloseDialog
                    },
                    onTabClick = onInactiveTabClick,
                    onTabCloseClick = onInactiveTabClose,
                    onCFRShown = onInactiveTabsCFRShown,
                    onCFRClick = onInactiveTabsCFRClick,
                    onCFRDismiss = onInactiveTabsCFRDismiss,
                )
            }
        }

        if (showInactiveTabsAutoCloseDialog) {
            onInactiveTabsAutoCloseDialogShown()
        }

        TabLayout(
            tabs = normalTabs,
            displayTabsInGrid = displayTabsInGrid,
            selectedTabId = selectedTabId,
            selectionMode = selectionMode,
            modifier = Modifier.testTag(TabsTrayTestTag.NORMAL_TABS_LIST),
            onTabClose = onTabClose,
            onItemClick = onItemClick,
            onItemLongClick = onItemLongClick,
            header = optionalInactiveTabsHeader,
            onTabDragStart = onTabDragStart,
            onMove = onMove,
        )
    } else {
        EmptyNormalTabsPage()
    }
}

/**
 * UI for displaying the empty state of the Normal Tabs Page in the Tab Manager.
 *
 * @param modifier The [Modifier] to be applied to the layout.
 */
@Composable
private fun EmptyNormalTabsPage(
    modifier: Modifier = Modifier,
) {
    EmptyTabPage(
        modifier = modifier.testTag(TabsTrayTestTag.EMPTY_NORMAL_TABS_LIST),
    ) {
        Column(
            modifier = Modifier.width(EmptyPageWidth),
            horizontalAlignment = Alignment.CenterHorizontally,
        ) {
            Icon(
                painter = painterResource(id = iconsR.drawable.mozac_ic_tab_24),
                contentDescription = null,
                modifier = Modifier.size(LocalResources.current.getDimensionPixelSize(com.android.car.navioui.R.dimen.dsDefaultHeight).dp),
            )

            Spacer(modifier = Modifier.height(8.dp))

            Text(
                text = stringResource(id = R.string.tab_manager_empty_normal_tabs_page_header),
                textAlign = TextAlign.Center,
                style = FirefoxTheme.typography.headline7,
            )
        }
    }
}

@FlexibleWindowLightDarkPreview
@Composable
private fun EmptyNormalTabsPagePreview() {
    FirefoxTheme {
        EmptyNormalTabsPage(modifier = Modifier.background(color = MaterialTheme.colorScheme.surface))
    }
}
