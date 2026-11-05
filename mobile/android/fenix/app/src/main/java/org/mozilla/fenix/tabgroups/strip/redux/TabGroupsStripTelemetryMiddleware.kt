/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.tabgroups.strip.redux

import mozilla.components.lib.state.Middleware
import mozilla.components.lib.state.Store

/** A [Middleware] that records telemetry for [TabGroupsStripAction]s dispatched to the [TabGroupsStripStore]. */
class TabGroupsStripTelemetryMiddleware : Middleware<TabGroupsStripState, TabGroupsStripAction> {
    override fun invoke(
        store: Store<TabGroupsStripState, TabGroupsStripAction>,
        next: (TabGroupsStripAction) -> Unit,
        action: TabGroupsStripAction,
    ) {
        next(action)
    }
}
