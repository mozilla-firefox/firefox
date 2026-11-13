/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
package org.mozilla.fenix.tabgroups.flow

import org.mozilla.fenix.tabstray.data.TabsTrayItem

/** List of valid destinations for the Tab Group feature flow outside of the TabsTray. */
sealed interface TabGroupFlowDestination {
    /** [TabGroupFlowDestination] representing the Root. Displays no content, is a base for the bottom sheet. */
    data object Root : TabGroupFlowDestination

    /** [TabGroupFlowDestination] representing the [EditTabGroup]. */
    data object EditTabGroup : TabGroupFlowDestination

    /** [TabGroupFlowDestination] for adding a tab to a group */
    data class AddToTabGroup(val tabId: String) : TabGroupFlowDestination

    /** [TabGroupFlowDestination] for moving a tab to a group. */
    data class MoveToTabGroup(val tabId: String) : TabGroupFlowDestination

    /**
     * [TabGroupFlowDestination] representing the [ExpandedTabGroup].
     *
     * @property group The displayed [TabsTrayItem.TabGroup].
     */
    data class ExpandedTabGroup(val group: TabsTrayItem.TabGroup) : TabGroupFlowDestination
}
