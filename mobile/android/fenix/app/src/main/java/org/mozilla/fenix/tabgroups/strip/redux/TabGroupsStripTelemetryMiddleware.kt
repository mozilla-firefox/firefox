/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.tabgroups.strip.redux

import mozilla.components.lib.state.Middleware
import mozilla.components.lib.state.Store
import org.mozilla.fenix.GleanMetrics.TabsTray

/**
 * A [Middleware] that records telemetry for [TabGroupsStripAction]s dispatched to the [TabGroupsStripStore].
 *
 * @param isHomepageAsNewTabEnabled Whether the homepage as a new tab (HNT) feature is enabled.
 */
class TabGroupsStripTelemetryMiddleware(private val isHomepageAsNewTabEnabled: () -> Boolean) :
    Middleware<TabGroupsStripState, TabGroupsStripAction> {
    override fun invoke(
        store: Store<TabGroupsStripState, TabGroupsStripAction>,
        next: (TabGroupsStripAction) -> Unit,
        action: TabGroupsStripAction,
    ) {
        val inGroup = store.state is TabGroupsStripState.TabGroupStripVisible

        next(action)

        when (action) {
            is TabGroupsStripAction.AddTabClicked ->
                if (inGroup && isHomepageAsNewTabEnabled()) {
                    TabsTray.tabAddedToGroup.record(TabsTray.TabAddedToGroupExtra(tabCount = 1))
                }
            is TabGroupsStripAction.InitializeDataObservation,
            is TabGroupsStripAction.TabClicked,
            is TabGroupsStripAction.TabGroupChanged,
            is TabGroupsStripAction.TabGroupCleared,
            is TabGroupsStripAction.ExpandGroupClicked -> Unit
        }
    }
}
