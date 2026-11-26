/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.components.menu

/** Test tags identifying the menu shown by [MenuFragment] in automated tests. */
internal object MenuTestTag {
    const val MAIN_MENU = "mainMenu"

    // The submenu header's back button has no visible text/label, so UI tests tap it by this tag.
    const val SUBMENU_HEADER_BACK_BUTTON = "submenu.header.back.button"
}
