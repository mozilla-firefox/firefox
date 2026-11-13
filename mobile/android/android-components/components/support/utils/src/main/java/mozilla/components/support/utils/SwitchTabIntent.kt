/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.support.utils

/** Contract of the intent asking the app to bring the browser back on a given tab. */
object SwitchTabIntent {
    /** Action of the intent. */
    const val ACTION_SWITCH_TAB = "mozac.support.utils.SWITCH_TAB"

    /** String extra holding the ID of the tab to switch to. */
    const val EXTRA_TAB_ID = "mozac.support.utils.TAB_ID"
}
