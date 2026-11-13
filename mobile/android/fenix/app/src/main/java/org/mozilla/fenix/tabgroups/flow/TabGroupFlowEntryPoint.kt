/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
package org.mozilla.fenix.tabgroups.flow

import android.os.Parcelable
import kotlinx.parcelize.Parcelize

/** Public API for entry points to the Tab Group flow. */
sealed interface TabGroupFlowEntryPoint : Parcelable {
    /** Name used for reporting entry point to telemetry. */
    val telemetryName: String

    /** Tab group flow entry point for adding a tab to a tab group. */
    @Parcelize
    data class AddTabToGroup(val tabId: String) : TabGroupFlowEntryPoint {
        override val telemetryName: String
            get() = "add_to_group"
    }

    /** Tab group flow entry point for moving a tab to a tab group. */
    @Parcelize
    data class MoveTabToGroup(val tabId: String) : TabGroupFlowEntryPoint {
        override val telemetryName: String
            get() = "move_to_group"
    }

    /** Tab group flow entry point for viewing a tab group. */
    @Parcelize
    data class ViewGroup(val groupId: String) : TabGroupFlowEntryPoint {
        override val telemetryName: String
            get() = "view_group"
    }

    /** Tab group flow entry point for editing a tab group. */
    @Parcelize
    data class EditGroup(val groupId: String) : TabGroupFlowEntryPoint {
        override val telemetryName: String
            get() = "edit_group"
    }
}
