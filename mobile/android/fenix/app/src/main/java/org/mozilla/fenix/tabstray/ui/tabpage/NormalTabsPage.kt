/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.tabstray.ui.tabpage

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.res.dimensionResource
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import mozilla.components.compose.base.annotation.FlexibleWindowLightDarkPreview
import mozilla.components.compose.draggable.ItemInteractionHandler
import mozilla.components.ui.icons.R as iconsR
import org.mozilla.fenix.R
import org.mozilla.fenix.tabstray.TabsTrayTestTag
import org.mozilla.fenix.tabstray.data.TabsTrayItem
import org.mozilla.fenix.tabstray.redux.state.TabsTrayState.DragProcessingState
import org.mozilla.fenix.tabstray.redux.state.TabsTrayState.InactiveTabsState
import org.mozilla.fenix.tabstray.redux.state.TabsTrayState.Mode
import org.mozilla.fenix.tabstray.redux.state.TabsTrayState.NormalTabsState
import org.mozilla.fenix.tabstray.redux.state.TabsTrayState.TabsTrayConfig
import org.mozilla.fenix.tabstray.telemetry.OnTabClose
import org.mozilla.fenix.tabstray.telemetry.TabCloseType
import org.mozilla.fenix.tabstray.ui.inactivetabs.InactiveTabsList
import org.mozilla.fenix.theme.FirefoxTheme
import org.mozilla.fenix.trackingprotection.TrackersBlockedCard

private val EmptyPageWidth = 170.dp

/**
 * UI for displaying the Normal Tabs Page in the Tab Manager.
 *
 * @param normalTabsState The current snapshot of [NormalTabsState].
 * @param inactiveTabsState The current snapshot of [InactiveTabsState].
 * @param selectionMode The current selection [Mode].
 * @param tabsTrayConfig The current snapshot of [TabsTrayConfig].
 * @param displayTabGroupOnboarding Whether onboarding for tab groups should be shown.
 * @param dragProcessingState The lifecycle state of tab-group drag handling
 * @param itemInteractionHandler Handles tab interactions, such as moves and drag and drop.
 * @param enteringGroupId The id of a group entering composition, if any. Can be null.
 * @param trackersBlockedCount The number of trackers blocked to display in the footer card.
 * @param focusEnabled Whether the focus indicator is enabled.
 * @param onTabClose Invoked when the user closes a tab, with the [TabCloseType] of the interaction.
 * @param onItemClick Invoked when the user clicks on a tab.
 * @param onItemLongClick Invoked when the user long clicks a tab.
 * @param shouldShowInactiveTabsAutoCloseDialog Whether the inactive tabs auto close dialog should be displayed.
 * @param onInactiveTabsHeaderClick Invoked when the user clicks on the inactive tabs section header.
 * @param onDeleteAllInactiveTabsClick Invoked when the user clicks on the delete all inactive tabs button.
 * @param onInactiveTabsAutoCloseDialogShown Invoked when the inactive tabs auto close dialog is presented to the user.
 * @param onInactiveTabAutoCloseDialogCloseButtonClick Invoked when the user clicks on the inactive tab auto close
 *   dialog's dismiss button.
 * @param onEnableInactiveTabAutoCloseClick Invoked when the user clicks on the inactive tab auto close dialog's enable
 *   button.
 * @param onInactiveTabClick Invoked when the user clicks on an inactive tab.
 * @param onInactiveTabClose Invoked when the user clicks on an inactive tab's close button.
 * @param shouldShowInactiveTabsCFR Returns whether the inactive tabs CFR is displayed.
 * @param onInactiveTabsCFRShown Invoked when the inactive tabs CFR is displayed.
 * @param onInactiveTabsCFRClick Invoked when the inactive tabs CFR is clicked.
 * @param onInactiveTabsCFRDismiss Invoked when the inactive tabs CFR is dismissed.
 * @param onEditTabGroupClick Invoked when the user clicks to edit a tab group.
 * @param onCloseTabGroupClick Invoked when the user clicks to close a tab group.
 * @param onShareTabGroupClick Invoked when the user clicks to share a tab group.
 * @param onDeleteTabGroupClick Invoked when the user clicks on delete tab group.
 * @param onUngroupTabGroupClick Invoked when the user clicks to ungroup a tab group.
 * @param onTabGroupOnboardingDismiss Invoked when the user dismisses the tab group onboarding card.
 * @param onTabGroupOnboardingShown Invoked when the tab group onboarding card is shown to the user.
 * @param onPrivacyReportTapped Invoked when the trackers blocked pill is tapped.
 * @param onEnteringGroupAnimationPlayed Invoked when the group entrance animation is played.
 */
@Composable
@Suppress("LongParameterList")
internal fun NormalTabsPage(
    normalTabsState: NormalTabsState,
    inactiveTabsState: InactiveTabsState,
    selectionMode: Mode,
    tabsTrayConfig: TabsTrayConfig,
    displayTabGroupOnboarding: Boolean,
    dragProcessingState: DragProcessingState,
    itemInteractionHandler: ItemInteractionHandler,
    enteringGroupId: String?,
    trackersBlockedCount: Int? = null,
    focusEnabled: Boolean,
    onTabClose: OnTabClose,
    onItemClick: (TabsTrayItem) -> Unit,
    onItemLongClick: (TabsTrayItem) -> Unit,
    shouldShowInactiveTabsAutoCloseDialog: Boolean,
    onInactiveTabsHeaderClick: (Boolean) -> Unit,
    onDeleteAllInactiveTabsClick: () -> Unit,
    onInactiveTabsAutoCloseDialogShown: () -> Unit,
    onInactiveTabAutoCloseDialogCloseButtonClick: () -> Unit,
    onEnableInactiveTabAutoCloseClick: () -> Unit,
    onInactiveTabClick: (TabsTrayItem.Tab) -> Unit,
    onInactiveTabClose: (TabsTrayItem.Tab) -> Unit,
    shouldShowInactiveTabsCFR: Boolean,
    onInactiveTabsCFRShown: () -> Unit,
    onInactiveTabsCFRClick: () -> Unit,
    onInactiveTabsCFRDismiss: () -> Unit,
    onEditTabGroupClick: (TabsTrayItem.TabGroup) -> Unit,
    onCloseTabGroupClick: (TabsTrayItem.TabGroup) -> Unit,
    onShareTabGroupClick: (TabsTrayItem.TabGroup) -> Unit,
    onDeleteTabGroupClick: (TabsTrayItem.TabGroup) -> Unit,
    onUngroupTabGroupClick: (TabsTrayItem.TabGroup) -> Unit,
    onTabGroupOnboardingDismiss: () -> Unit,
    onTabGroupOnboardingShown: () -> Unit,
    onPrivacyReportTapped: (() -> Unit)? = null,
    onEnteringGroupAnimationPlayed: () -> Unit,
) {
    if (normalTabsState.items.isNotEmpty() || inactiveTabsState.tabs.isNotEmpty()) {
        var showAutoCloseDialog by remember { mutableStateOf(shouldShowInactiveTabsAutoCloseDialog) }

        val optionalInactiveTabsHeader: (@Composable () -> Unit)? =
            if (inactiveTabsState.tabs.isEmpty()) {
                null
            } else {
                {
                    InactiveTabsList(
                        inactiveTabs = inactiveTabsState.tabs,
                        expanded = inactiveTabsState.isExpanded,
                        showAutoCloseDialog = showAutoCloseDialog,
                        showCFR = shouldShowInactiveTabsCFR,
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
                    if (!tabsTrayConfig.displayTabsInGrid) {
                        Spacer(modifier = Modifier.height(FirefoxTheme.layout.space.static200))
                    }
                }
            }

        if (shouldShowInactiveTabsAutoCloseDialog) {
            onInactiveTabsAutoCloseDialogShown()
        }

        TabLayout(
            tabs = normalTabsState.items,
            displayTabsInGrid = tabsTrayConfig.displayTabsInGrid,
            dragAndDropEnabled = tabsTrayConfig.tabGroupsDragAndDropEnabled,
            liveReorderEnabled = tabsTrayConfig.tabGroupsLiveReorderEnabled,
            displayTabGroupOnboarding = displayTabGroupOnboarding,
            selectedItemIndex = normalTabsState.selectedItemIndex,
            selectionMode = selectionMode,
            trackersBlockedCount = trackersBlockedCount,
            modifier = Modifier.testTag(TabsTrayTestTag.NORMAL_TABS_LIST),
            onTabClose = onTabClose,
            onItemClick = onItemClick,
            onItemLongClick = onItemLongClick,
            header = optionalInactiveTabsHeader,
            onEditTabGroupClick = onEditTabGroupClick,
            onCloseTabGroupClick = onCloseTabGroupClick,
            onShareTabGroupClick = onShareTabGroupClick,
            onDeleteTabGroupClick = onDeleteTabGroupClick,
            onUngroupTabGroupClick = onUngroupTabGroupClick,
            onTabGroupOnboardingDismiss = onTabGroupOnboardingDismiss,
            onTabGroupOnboardingShown = onTabGroupOnboardingShown,
            itemInteractionHandler = itemInteractionHandler,
            focusEnabled = focusEnabled,
            onPrivacyReportTapped = onPrivacyReportTapped,
            enteringGroupId = enteringGroupId,
            onGroupEntranceAnimationPlayed = onEnteringGroupAnimationPlayed,
            dragProcessingState = dragProcessingState,
        )
    } else {
        EmptyNormalTabsPage(
            trackersBlockedCount = trackersBlockedCount,
            onPrivacyReportTapped = onPrivacyReportTapped,
        )
    }
}

/**
 * UI for displaying the empty state of the Normal Tabs Page in the Tab Manager.
 *
 * @param modifier The [Modifier] to be applied to the layout.
 * @param trackersBlockedCount The number of trackers blocked to display in the footer card.
 * @param onPrivacyReportTapped Invoked when the trackers blocked pill is tapped.
 */
@Composable
private fun EmptyNormalTabsPage(
    modifier: Modifier = Modifier,
    trackersBlockedCount: Int? = null,
    onPrivacyReportTapped: (() -> Unit)? = null,
) {
    val bottomBarHeight = dimensionResource(id = R.dimen.browser_toolbar_height)

    Column(
        modifier = modifier.fillMaxSize().testTag(TabsTrayTestTag.EMPTY_NORMAL_TABS_LIST),
        horizontalAlignment = Alignment.CenterHorizontally,
    ) {
        EmptyTabPage(modifier = Modifier.weight(1f)) {
            Column(
                modifier = Modifier.width(EmptyPageWidth),
                horizontalAlignment = Alignment.CenterHorizontally,
            ) {
                Icon(
                    painter = painterResource(id = iconsR.drawable.mozac_ic_tab_24),
                    contentDescription = null,
                    modifier = Modifier.size(72.dp),
                )

                Spacer(modifier = Modifier.height(8.dp))

                Text(
                    text = stringResource(id = R.string.tab_manager_empty_normal_tabs_page_header),
                    textAlign = TextAlign.Center,
                    style = FirefoxTheme.typography.headline7,
                )
            }
        }

        if (trackersBlockedCount != null) {
            TrackersBlockedCard(
                trackersBlockedCount = trackersBlockedCount,
                onPrivacyReportTapped = onPrivacyReportTapped,
            )
            Spacer(modifier = Modifier.height(bottomBarHeight + 32.dp))
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
