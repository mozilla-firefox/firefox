/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
package org.mozilla.fenix.tabgroups.flow

import mozilla.components.lib.state.Middleware
import mozilla.components.lib.state.Store
import org.mozilla.fenix.components.metrics.MetricsUtils
import org.mozilla.fenix.tabgroups.TabGroupTelemetry
import org.mozilla.fenix.tabstray.redux.action.TabGroupAction

/** Class that handles telemetry for the tab group flow. */
class TabGroupFlowTelemetryMiddleware : Middleware<TabGroupFlowState, TabGroupAction> {

    private val tabGroupTelemetry: TabGroupTelemetry =
        TabGroupTelemetry(tabGroupAccessPoint = MetricsUtils.TabGroupAccessPoint.BROWSER_MENU)

    override fun invoke(
        store: Store<TabGroupFlowState, TabGroupAction>,
        next: (TabGroupAction) -> Unit,
        action: TabGroupAction,
    ) {
        handleTabGroupAction(store, action)
        next(action)
    }

    private fun handleTabGroupAction(store: Store<TabGroupFlowState, TabGroupAction>, action: TabGroupAction) {
        when (action) {
            is TabGroupAction.ThemeChanged -> {
                tabGroupTelemetry.recordThemeChanged(
                    context = store.state,
                    theme = action.theme,
                )
            }

            TabGroupAction.SaveClicked -> {
                tabGroupTelemetry.recordSaveClicked(context = store.state)
            }

            is TabGroupAction.TabAddedToExistingTabGroup -> {
                tabGroupTelemetry.recordTabAddedToGroup(1)
            }

            is TabGroupAction.TabAddedToNewTabGroup -> {
                tabGroupTelemetry.recordTabAddedToGroup(1)
            }

            TabGroupAction.AddToNewTabGroup,
            TabGroupAction.AddToTabGroup,
            is TabGroupAction.CloseTabAndDeleteGroupConfirmed,
            is TabGroupAction.CloseTabGroupClicked,
            is TabGroupAction.DeleteClicked,
            is TabGroupAction.DeleteConfirmed,
            is TabGroupAction.DragAndDropInitiated,
            TabGroupAction.DragAndDropProcessed,
            is TabGroupAction.DragAndDropTwoTabs,
            is TabGroupAction.EditTabGroupClicked,
            TabGroupAction.NavigateBackInvoked,
            TabGroupAction.NewGroupAnimationFinished,
            is TabGroupAction.NewGroupCreated,
            TabGroupAction.NewTabGroupFabClicked,
            TabGroupAction.NewTabGroupMenuClicked,
            TabGroupAction.OnboardingDismissed,
            TabGroupAction.OnboardingShown,
            is TabGroupAction.OpenCreatedTabGroup,
            is TabGroupAction.OpenTabGroupClicked,
            is TabGroupAction.SelectedTabsAddedToGroup,
            is TabGroupAction.TabClosed,
            is TabGroupAction.TabGroupClicked,
            is TabGroupAction.UngroupRequested,
            is TabGroupAction.UngroupConfirmed,
            is TabGroupAction.UngroupConfirmationRequested,
            is TabGroupAction.NameChanged,
            is TabGroupAction.TabGroupsUpdated,
            is TabGroupAction.CollectionsMigrationCardDismissed -> {}
        }
    }
}
