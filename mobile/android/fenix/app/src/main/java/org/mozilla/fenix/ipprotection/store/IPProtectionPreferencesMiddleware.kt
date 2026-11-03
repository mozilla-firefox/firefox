/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.ipprotection.store

import kotlin.time.Instant
import mozilla.components.ExperimentalAndroidComponentsApi
import mozilla.components.feature.ipprotection.store.IPProtectionAction
import mozilla.components.feature.ipprotection.store.state.IPProtectionState
import mozilla.components.lib.state.Middleware
import mozilla.components.lib.state.Store
import org.mozilla.fenix.utils.Settings

/** Interface providing access to IP Protection related [android.content.SharedPreferences]. */
interface IPProtectionRepository {
    var hasAlreadyUsedVpn: Boolean
    var lastDataLimitNoticeCycleResetTime: Instant?
}

/** The default implementation of [IPProtectionRepository] proxying calls to global Fenix [Settings]. */
class DefaultIPProtectionRepository(private val settings: Settings) : IPProtectionRepository {
    override var hasAlreadyUsedVpn: Boolean
        get() = settings.hasAlreadyUsedVpn
        set(value) {
            settings.hasAlreadyUsedVpn = value
        }

    override var lastDataLimitNoticeCycleResetTime: Instant?
        get() = settings.ipProtectionLastDataLimitNoticeCycleResetTime?.let(Instant::parseOrNull)
        set(value) {
            settings.ipProtectionLastDataLimitNoticeCycleResetTime = value?.toString()
        }
}

/** [Middleware] responsible for intercepting a toggle action and persistently recording the initial VPN interaction. */
@OptIn(ExperimentalAndroidComponentsApi::class)
internal class IPProtectionPreferencesMiddleware(private val preferences: IPProtectionRepository) :
    Middleware<IPProtectionState, IPProtectionAction> {

    override fun invoke(
        store: Store<IPProtectionState, IPProtectionAction>,
        next: (IPProtectionAction) -> Unit,
        action: IPProtectionAction,
    ) {
        next(action)

        if (action is IPProtectionAction.ActivationAction.Toggle && !preferences.hasAlreadyUsedVpn) {
            preferences.hasAlreadyUsedVpn = true
        }
    }
}
