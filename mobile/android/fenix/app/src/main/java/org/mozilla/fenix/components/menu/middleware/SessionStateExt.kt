/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.components.menu.middleware

import mozilla.components.browser.state.ext.getUrl
import mozilla.components.browser.state.state.CustomTabSessionState
import mozilla.components.browser.state.state.SessionState
import mozilla.components.browser.state.state.TabSessionState

/**
 * Gets the tab url from the [SessionState] based on the type of the session state.
 *
 * For [TabSessionState] (a non-custom tab), we use the extension helper function which handles the reader state url
 * when necessary. For [CustomTabSessionState], we use the content url, since we cannot have reader mode in custom tabs.
 */
internal fun SessionState.getTabUrl(): String? {
    return when (this) {
        is TabSessionState -> this.getUrl()
        // intentionally kept this for visibility instead of collapsing into the else branch
        is CustomTabSessionState -> content.url
        else -> content.url
    }
}
