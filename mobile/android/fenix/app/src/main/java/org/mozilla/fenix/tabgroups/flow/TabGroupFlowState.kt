/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
package org.mozilla.fenix.tabgroups.flow

import androidx.compose.runtime.Immutable
import mozilla.components.lib.state.State
import org.mozilla.fenix.tabgroups.TabGroupTelemetry
import org.mozilla.fenix.tabstray.data.TabsTrayItem
import org.mozilla.fenix.tabstray.redux.state.TabGroupFormState
import org.mozilla.fenix.tabstray.redux.state.TabsTrayState
import org.mozilla.fenix.tabstray.redux.state.newTabGroupForm

/**
 * @property entryPoint The entry point into this flow. Used for displaying context-aware messages to a user.
 * @property backStack The flow's backstack
 * @property groups The list of groups for display or selection.
 * @property formState The state of the tab group edit form, for creating or updating a group.
 * @property config Tracks user configuration options impacting the flow.
 * @property tabPendingNewGroup A tab id that is pending addition to a newly created TabGroup. May be null.
 */
@Immutable
data class TabGroupFlowState(
    val entryPoint: TabGroupFlowEntryPoint,
    val backStack: List<TabGroupFlowDestination> = listOf(TabGroupFlowDestination.Root),
    override val groups: List<TabsTrayItem.TabGroup> = emptyList(),
    override val formState: TabGroupFormState? = null,
    val config: Config = Config(),
    val tabPendingNewGroup: String? = null,
) : State, TabGroupTelemetry.TabGroupTelemetryContext {
    /** Drops the last entry of [TabsTrayState.backStack]. If [backStack] only has one entry, no changes occur. */
    internal fun popBackStack(): List<TabGroupFlowDestination> =
        if (backStack.size > 1) {
            backStack.dropLast(1)
        } else {
            backStack
        }

    /**
     * Configuration for the flow of Tab Groups outside the TabsTray.
     *
     * @property displayTabsInGrid Whether the group's tabs are displayed in a grid (vs list).
     * @property homepageAsNewTabEnabled Whether the homepage as a new tab feature is enabled, which gates the ability
     *   to open new tabs within an expanded tab group.
     */
    @Immutable
    data class Config(
        val displayTabsInGrid: Boolean = false,
        val homepageAsNewTabEnabled: Boolean = false,
    )
}

/** Returns an initial [TabGroupFormState] derived from [TabGroupFlowState]. */
fun TabGroupFlowState.initializeTabGroupForm(isStarterTabGroup: Boolean = false) =
    newTabGroupForm(existingGroups = groups, isStarterTabGroup = isStarterTabGroup)

/** Returns the groups [tabId] can be moved to, excluding the group it's currently in. */
fun TabGroupFlowState.moveTargetGroups(tabId: String): List<TabsTrayItem.TabGroup> = groups.filter { group ->
    group.tabs.none { it.id == tabId }
}
