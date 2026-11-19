/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.tabstray.telemetry

import org.mozilla.fenix.tabstray.data.TabsTrayItem

/**
 * Enum representing the interaction a user performed to close a tab.
 *
 * @property telemetryId The telemetry identifier.
 */
enum class TabCloseType(val telemetryId: String) {
    /** The tab's close button was tapped. */
    TAP("tap"),

    /** The tab was swiped away. */
    SWIPE("swipe"),

    /** The interaction that started the closure is no longer known, e.g. after a confirmation dialog. */
    UNKNOWN("unknown"),
}

/** Invoked when a [TabsTrayItem.Tab] is closed, along with the [TabCloseType] of the interaction that closed it. */
typealias OnTabClose = (TabsTrayItem.Tab, TabCloseType) -> Unit
