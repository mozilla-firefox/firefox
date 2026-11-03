/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.compose.menu.ui.customize

/** How a group is shown in the customize menu screen. */
internal enum class CustomizeMenuGroupState {
    /** The group is part of the menu and can be removed from it. */
    SHOWN,

    /** The group is part of the menu and cannot be removed from it. */
    MANDATORY,

    /** The group was removed from the menu. */
    HIDDEN,
}
