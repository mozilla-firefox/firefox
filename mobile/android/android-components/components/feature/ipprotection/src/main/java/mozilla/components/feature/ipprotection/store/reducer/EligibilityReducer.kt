/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

@file:OptIn(ExperimentalAndroidComponentsApi::class)

package mozilla.components.feature.ipprotection.store.reducer

import mozilla.components.ExperimentalAndroidComponentsApi
import mozilla.components.feature.ipprotection.store.IPProtectionAction
import mozilla.components.feature.ipprotection.store.state.IPProtectionState

internal object EligibilityReducer {
    fun reduce(
        state: IPProtectionState,
        action: IPProtectionAction.EligibilityAction,
    ): IPProtectionState =
        when (action) {
            is IPProtectionAction.EligibilityAction.EligibilityChanged -> {
                state.copy(eligibilityStatus = action.eligibility)
            }
        }
}
