/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.ipprotection.store.reducer

import mozilla.components.feature.ipprotection.store.IPProtectionAction
import mozilla.components.feature.ipprotection.store.state.IPProtectionState

internal fun iPProtectionReducer(
    state: IPProtectionState,
    action: IPProtectionAction,
): IPProtectionState =
    when (action) {
        is IPProtectionAction.EligibilityAction -> EligibilityReducer.reduce(state, action)
        is IPProtectionAction.ServiceAction -> ServiceReducer.reduce(state, action)
        is IPProtectionAction.ProxyAction -> ProxyReducer.reduce(state, action)
        is IPProtectionAction.UsageAction -> UsageReducer.reduce(state, action)
        is IPProtectionAction.AccountAction -> AccountReducer.reduce(state, action)
        is IPProtectionAction.ActivationAction -> ActivationReducer.reduce(state, action)
        is IPProtectionAction.LocationAction -> LocationReducer.reduce(state, action)
    }
