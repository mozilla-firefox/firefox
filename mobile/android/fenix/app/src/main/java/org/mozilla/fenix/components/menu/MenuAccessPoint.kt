/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.components.menu

import org.mozilla.fenix.components.accounts.FenixFxAEntryPoint

/** The origin access points that was used to navigate to the Menu dialog. */
enum class MenuAccessPoint {
    /** Menu was accessed from the browser. */
    Browser,

    /** Menu was accessed from an external app (e.g. custom tab). */
    External,

    /** Menu was accessed from the home screen. */
    Home,

    /** Menu was accessed in preview mode. */
    Preview,
}

/** Menu navigation destination. */
enum class Route {
    MainMenu,
    CustomTabMenu,
}

/** Returns the [FenixFxAEntryPoint] equivalent from the given [MenuAccessPoint]. */
internal fun MenuAccessPoint.toFenixFxAEntryPoint(): FenixFxAEntryPoint {
    return when (this) {
        MenuAccessPoint.Browser,
        MenuAccessPoint.Preview -> FenixFxAEntryPoint.BrowserToolbar
        MenuAccessPoint.External -> FenixFxAEntryPoint.ExternalView
        MenuAccessPoint.Home -> FenixFxAEntryPoint.HomeMenu
    }
}
