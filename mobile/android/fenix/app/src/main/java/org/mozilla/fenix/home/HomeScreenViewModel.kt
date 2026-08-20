/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.home

import androidx.lifecycle.ViewModel

/** [ViewModel] that shares data on the sessions to delete between various fragments. */
class HomeScreenViewModel : ViewModel() {
    /**
     * The session ID or a session code specified by [ALL_NORMAL_TABS] or [ALL_PRIVATE_TABS] to queue the sessions to
     * delete once the home screen is resumed.
     */
    var sessionToDelete: String? = null

    /** Contains constants used by [sessionToDelete]. */
    companion object {
        /** Session code to use in [sessionToDelete] to specify all normal tabs should be deleted. */
        const val ALL_NORMAL_TABS = "all_normal"

        /** Session code to use in [sessionToDelete] to specify all active normal tabs should be deleted. */
        const val ALL_ACTIVE_NORMAL_TABS = "all_active_normal"

        /** Session code to use in [sessionToDelete] to specify all private tabs should be deleted. */
        const val ALL_PRIVATE_TABS = "all_private"
    }
}
