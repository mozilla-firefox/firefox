/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.components.menu

import mozilla.components.browser.state.selector.findCustomTab
import mozilla.components.browser.state.selector.selectedTab
import mozilla.components.browser.state.state.BrowserState
import mozilla.components.browser.state.state.SessionState

/** For what target will the menu be shown */
sealed interface MenuTarget {
    /** Get the [SessionState], if any, that should be targeted by this menu. */
    fun browserSessionFrom(state: BrowserState): SessionState?

    /** The menu will be shown for the home screen, so its menu items should target no tab. */
    data object Home : MenuTarget {
        override fun browserSessionFrom(state: BrowserState): SessionState? = null
    }

    /** The menu will be shown in the browser screen, so its menu items should target the current tab. */
    data object BrowserTab : MenuTarget {
        override fun browserSessionFrom(state: BrowserState): SessionState? = state.selectedTab
    }

    /** The menu will be shown for a custom tab, so its menu items should target that custom tab. */
    data class CustomTab(val customTabId: String, val isSandboxed: Boolean = false) : MenuTarget {
        override fun browserSessionFrom(state: BrowserState): SessionState? = state.findCustomTab(customTabId)
    }

    /** The menu will be shown in the menu customization screen. */
    data object CustomizeMenu : MenuTarget {
        override fun browserSessionFrom(state: BrowserState): SessionState? = null
    }

    /** The menu will be shown in the menu preview screen. */
    data object Preview : MenuTarget {
        override fun browserSessionFrom(state: BrowserState): SessionState? = null
    }
}
