/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.tabstray

import mozilla.components.lib.state.Middleware
import mozilla.components.lib.state.Store
import mozilla.telemetry.glean.private.NoExtras
import org.mozilla.experiments.nimbus.NimbusEventStore
import org.mozilla.fenix.GleanMetrics.Metrics
import org.mozilla.fenix.GleanMetrics.TabSearch
import org.mozilla.fenix.GleanMetrics.TabsTray
import org.mozilla.fenix.components.metrics.MetricsUtils
import org.mozilla.fenix.components.metrics.MetricsUtils.BookmarkAction.Source
import org.mozilla.fenix.tabgroups.TabGroupTelemetry
import org.mozilla.fenix.tabstray.data.TabsTrayItem
import org.mozilla.fenix.tabstray.navigation.TabManagerNavDestination
import org.mozilla.fenix.tabstray.redux.action.TabGroupAction
import org.mozilla.fenix.tabstray.redux.action.TabSearchAction
import org.mozilla.fenix.tabstray.redux.action.TabsTrayAction
import org.mozilla.fenix.tabstray.redux.state.Page
import org.mozilla.fenix.tabstray.redux.state.TabsTrayState

/**
 * Middleware that records telemetry events for the Tabs Tray feature.
 *
 * @param nimbusEventStore [NimbusEventStore] for recording events to use in behavioral targeting.
 */
class TabsTrayTelemetryMiddleware(private val nimbusEventStore: NimbusEventStore) :
    Middleware<TabsTrayState, TabsTrayAction> {

    private var shouldReportInactiveTabMetrics: Boolean = true
    private val tabGroupTelemetry: TabGroupTelemetry =
        TabGroupTelemetry(tabGroupAccessPoint = MetricsUtils.TabGroupAccessPoint.TABS_TRAY)

    override fun invoke(
        store: Store<TabsTrayState, TabsTrayAction>,
        next: (TabsTrayAction) -> Unit,
        action: TabsTrayAction,
    ) {
        val topDestination = store.state.backStack.lastOrNull()
        val isEditing = store.state.tabGroupState.formState?.inEditState == true

        requireNotNull(topDestination) { "The backstack cannot be empty" }

        // these actions need to be handled prior to invoking next(action)
        when (action) {
            is TabGroupAction -> handleTabGroupAction(store, action)
            is TabsTrayAction.TabItemLongClicked -> handleTabItemLongClicked(store, action)
            else -> {}
        }

        next(action)

        when (action) {
            is TabSearchAction -> handleTabSearchAction(action)
            is TabsTrayAction.NavigateBackInvoked -> handleNavigateBackInvoked(topDestination, isEditing)
            // ignore the actions that were handled prior
            is TabGroupAction,
            is TabsTrayAction.TabItemLongClicked -> {}
            else -> {
                handleGeneralTabsTrayAction(store, action)
            }
        }
    }

    private fun handleGeneralTabsTrayAction(
        store: Store<TabsTrayState, TabsTrayAction>,
        action: TabsTrayAction,
    ) {
        when (action) {
            is TabsTrayAction.TabDataUpdateReceived -> {
                if (shouldReportInactiveTabMetrics) {
                    shouldReportInactiveTabMetrics = false

                    TabsTray.hasInactiveTabs.record(
                        TabsTray.HasInactiveTabsExtra(action.tabStorageUpdate.inactiveTabs.size)
                    )
                    Metrics.inactiveTabsCount.set(action.tabStorageUpdate.inactiveTabs.size.toLong())
                }
            }

            is TabsTrayAction.EnterSelectMode -> {
                TabsTray.enterMultiselectMode.record(TabsTray.EnterMultiselectModeExtra(false))
            }

            is TabsTrayAction.AddSelectTab -> {
                TabsTray.enterMultiselectMode.record(TabsTray.EnterMultiselectModeExtra(true))
            }

            is TabsTrayAction.TabAutoCloseDialogShown -> {
                TabsTray.autoCloseSeen.record(NoExtras())
            }

            is TabsTrayAction.ShareAllNormalTabs,
            is TabsTrayAction.ShareAllPrivateTabs -> {
                TabsTray.shareAllTabs.record(NoExtras())
            }

            is TabsTrayAction.SelectAllNormalTabs -> {
                TabsTray.selectAllNormalTabs.record(NoExtras())
            }

            is TabsTrayAction.CloseAllNormalTabs,
            is TabsTrayAction.CloseAllPrivateTabs -> {
                TabsTray.closeAllTabs.record(NoExtras())
            }

            is TabsTrayAction.BookmarkSelectedTabs -> {
                TabsTray.bookmarkSelectedTabs.record(TabsTray.BookmarkSelectedTabsExtra(tabCount = action.tabCount))
                MetricsUtils.recordBookmarkAddMetric(Source.TABS_TRAY, nimbusEventStore, count = action.tabCount)
            }

            is TabsTrayAction.ThreeDotMenuShown -> {
                TabsTray.menuOpened.record(NoExtras())
            }

            is TabsTrayAction.TabSearchClicked -> {
                TabSearch.tabSearchIconClicked.record(NoExtras())
            }

            is TabsTrayAction.PageSelected -> {
                if (action.page == Page.TabGroups) {
                    TabsTray.tabGroupScreenOpened.record(NoExtras())
                }
            }

            is TabsTrayAction.TabDragStart -> handleTabDragStart(store, action)

            is TabsTrayAction.ReorderTabsTrayItem -> handleReorderTabsTrayItem(store, action)

            TabsTrayAction.InitAction,
            is TabsTrayAction.PersistedUiStateUpdateReceived,
            TabsTrayAction.ExitSelectMode,
            is TabsTrayAction.RemoveSelectTab,
            TabsTrayAction.SyncNow,
            TabsTrayAction.SyncCompleted,
            is TabsTrayAction.UpdateInactiveExpanded,
            is TabsTrayAction.UpdateSyncedTabs,
            is TabsTrayAction.UpdateSelectedTabId,
            is TabsTrayAction.SyncedTabsHeaderToggled,
            TabsTrayAction.NavigateBackInvoked,
            is TabsTrayAction.UpdatePbmLockStatus,
            TabsTrayAction.DismissInactiveTabsCFR,
            TabsTrayAction.DismissInactiveTabsAutoCloseDialog,
            TabsTrayAction.TabDragCancel,
            is TabsTrayAction.TabItemLongClicked,
            is TabGroupAction,
            is TabSearchAction -> {}
        }
    }

    private fun handleTabDragStart(
        store: Store<TabsTrayState, TabsTrayAction>,
        action: TabsTrayAction.TabDragStart,
    ) {
        val itemType =
            when (store.state.normalTabsState.items.find { it.id == action.sourceId }) {
                is TabsTrayItem.TabGroup -> TabItemType.TAB_GROUP.telemetryId
                is TabsTrayItem.Tab -> TabItemType.TAB.telemetryId
                null -> TabItemType.UNKNOWN.telemetryId
            }
        TabsTray.tabLongPressDrag.record(TabsTray.TabLongPressDragExtra(itemType = itemType))
    }

    private fun handleReorderTabsTrayItem(
        store: Store<TabsTrayState, TabsTrayAction>,
        action: TabsTrayAction.ReorderTabsTrayItem,
    ) {
        val isTabGroup = store.state.normalTabsState.items.find { it.id == action.sourceId } is TabsTrayItem.TabGroup
        if (!isTabGroup) {
            TabsTray.tabLongPressDragRearrangedPosition.record(NoExtras())
        }
    }

    private fun handleTabItemLongClicked(
        store: Store<TabsTrayState, TabsTrayAction>,
        action: TabsTrayAction.TabItemLongClicked,
    ) {
        // We should record a long press even if we are not entering multi-select mode.
        // E.g. you can still long press and drag to reorder in private mode, which does not support multi-selection.
        TabsTray.tabLongPress.record(NoExtras())
        // Note that the selected tab check is also executed in TabsTrayReducer
        // and should be updated if this business logic ever changes.
        if (store.state.mode.selectedTabs.isNotEmpty()) {
            return
        }
        when (action.item) {
            is TabsTrayItem.TabGroup -> {
                TabsTray.enterMultiselectMode.record(TabsTray.EnterMultiselectModeExtra(true))
            }

            is TabsTrayItem.Tab -> {
                // Private tabs cannot be multi-selected
                if (!action.item.private) {
                    TabsTray.enterMultiselectMode.record(TabsTray.EnterMultiselectModeExtra(true))
                }
            }
        }
    }

    private fun handleTabGroupAction(
        store: Store<TabsTrayState, TabsTrayAction>,
        action: TabGroupAction,
    ) {
        when (action) {
            is TabGroupAction.EditTabGroupClicked -> {
                TabsTray.tabGroupEdited.record(NoExtras())
            }

            is TabGroupAction.SaveClicked,
            is TabGroupAction.ThemeChanged -> {
                handleTabGroupFormAction(store, action)
            }

            is TabGroupAction.DeleteConfirmed,
            is TabGroupAction.CloseTabAndDeleteGroupConfirmed -> {
                TabsTray.tabGroupDeleted.record(NoExtras())
            }

            is TabGroupAction.UngroupConfirmed -> {
                TabsTray.tabGroupUngrouped.record(NoExtras())

                if (action.dontAskAgain && !store.state.tabGroupState.skipUngroupConfirmation) {
                    TabsTray.ungroupConfirmationDisabled.record(NoExtras())
                }
            }

            is TabGroupAction.TabAddedToExistingTabGroup,
            is TabGroupAction.SelectedTabsAddedToGroup -> {
                handleTabAdditionToGroupAction(store, action)
            }

            is TabGroupAction.TabGroupClicked -> {
                if (store.state.mode is TabsTrayState.Mode.Normal) {
                    val sourceScreen =
                        if (store.state.selectedPage == Page.TabGroups) {
                            "group_screen"
                        } else {
                            "tab_screen"
                        }

                    TabsTray.tabGroupOpened.record(TabsTray.TabGroupOpenedExtra(source = sourceScreen))
                }
            }

            is TabGroupAction.AddToNewTabGroup,
            is TabGroupAction.NewTabGroupFabClicked,
            is TabGroupAction.NewTabGroupMenuClicked,
            is TabGroupAction.DragAndDropInitiated -> {
                handleTabGroupCreationAction(store, action)
            }

            is TabGroupAction.CloseTabGroupClicked -> {
                TabsTray.tabGroupClosed.record(NoExtras())
            }

            is TabGroupAction.OnboardingShown -> {
                TabsTray.tabGroupOnboardingShown.record(NoExtras())
            }

            is TabGroupAction.OnboardingDismissed -> {
                TabsTray.tabGroupOnboardingDismissed.record(NoExtras())
            }

            else -> {
                // no-op
            }
        }
    }

    private fun handleTabGroupFormAction(
        store: Store<TabsTrayState, TabsTrayAction>,
        action: TabGroupAction,
    ) {
        when (action) {
            is TabGroupAction.SaveClicked -> {
                tabGroupTelemetry.recordSaveClicked(context = store.state.tabGroupState)
            }

            is TabGroupAction.ThemeChanged -> {
                tabGroupTelemetry.recordThemeChanged(
                    context = store.state.tabGroupState,
                    theme = action.theme,
                )
            }

            else -> {
                // no-op
            }
        }
    }

    private fun handleTabAdditionToGroupAction(
        store: Store<TabsTrayState, TabsTrayAction>,
        action: TabGroupAction,
    ) {
        when (action) {
            is TabGroupAction.TabAddedToExistingTabGroup -> {
                tabGroupTelemetry.recordTabAddedToGroup(tabCount = 1)
            }

            is TabGroupAction.SelectedTabsAddedToGroup -> {
                tabGroupTelemetry.recordTabAddedToGroup(tabCount = store.state.mode.selectedTabs.size)
            }

            else -> {
                // no-op
            }
        }
    }

    private fun handleTabGroupCreationAction(
        store: Store<TabsTrayState, TabsTrayAction>,
        action: TabGroupAction,
    ) {
        when (action) {
            is TabGroupAction.AddToNewTabGroup,
            is TabGroupAction.NewTabGroupMenuClicked -> {
                tabGroupTelemetry.recordCreatedFromMenu()
            }

            is TabGroupAction.NewTabGroupFabClicked -> {
                tabGroupTelemetry.recordCreatedFromFab()
            }

            is TabGroupAction.DragAndDropInitiated -> {
                val isDraggingOntoTab =
                    store.state.normalTabsState.items.find { it.id == action.destinationId } is TabsTrayItem.Tab
                if (isDraggingOntoTab) {
                    tabGroupTelemetry.recordCreatedFromDragAndDrop()
                }
            }

            else -> {
                // no-op
            }
        }
    }

    private fun handleTabSearchAction(action: TabSearchAction) {
        when (action) {
            is TabSearchAction.SearchResultClicked -> {
                TabSearch.resultClicked.record(NoExtras())
            }

            else -> {
                // no-op
            }
        }
    }

    private fun handleNavigateBackInvoked(
        topDestination: TabManagerNavDestination,
        isEditing: Boolean,
    ) {
        when (topDestination) {
            is TabManagerNavDestination.TabSearch -> {
                TabSearch.navigateBackIconClicked.record(NoExtras())
            }

            is TabManagerNavDestination.AddToTabGroup -> {
                TabsTray.tabGroupCreateCancel.record(NoExtras())
            }

            is TabManagerNavDestination.EditTabGroup -> {
                if (!isEditing) {
                    TabsTray.tabGroupCreateCancel.record(NoExtras())
                }
            }

            else -> {
                // no-op
            }
        }
    }

    /**
     * Enum representing the type of tabs tray item.
     *
     * @property telemetryId The telemetry identifier.
     */
    enum class TabItemType(val telemetryId: String) {
        TAB("tab"),
        TAB_GROUP("tab_group"),
        UNKNOWN("unknown"),
    }
}
