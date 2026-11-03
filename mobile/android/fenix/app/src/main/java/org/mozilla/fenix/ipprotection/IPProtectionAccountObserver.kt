/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.ipprotection

import mozilla.components.concept.sync.AccountObserver
import org.mozilla.fenix.ipprotection.store.IPProtectionRepository

/**
 * Clears account scoped IP Protection preferences when the user signs out.
 *
 * @param repository [IPProtectionRepository] providing access to IP Protection related
 *   [android.content.SharedPreferences].
 */
class IPProtectionAccountObserver(private val repository: IPProtectionRepository) : AccountObserver {
    override fun onLoggedOut() {
        repository.lastDataLimitNoticeCycleResetTime = null
    }
}
