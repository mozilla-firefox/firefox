/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.components.menu

import androidx.annotation.VisibleForTesting
import androidx.annotation.VisibleForTesting.Companion.PRIVATE
import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.distinctUntilChanged
import kotlinx.coroutines.flow.map
import mozilla.components.feature.ipprotection.AuthenticationRequired
import mozilla.components.feature.ipprotection.AuthorizationRequired
import mozilla.components.feature.ipprotection.Authorized
import mozilla.components.feature.ipprotection.IPProtectionState
import mozilla.components.feature.ipprotection.IPProtectionStore
import mozilla.components.feature.ipprotection.ProxyStatus
import mozilla.components.feature.ipprotection.Uninitialized
import mozilla.components.lib.state.Store
import mozilla.components.lib.state.helpers.AbstractBinding
import org.mozilla.fenix.components.menu.store.IPProtectionMenuState
import org.mozilla.fenix.components.menu.store.IPProtectionMenuStatus
import org.mozilla.fenix.components.menu.store.MenuAction
import org.mozilla.fenix.components.menu.store.MenuState
import org.mozilla.fenix.components.menu.store.MenuStore

@VisibleForTesting(otherwise = PRIVATE)
internal const val BYTES_PER_GB = 1024 * 1024 * 1024f

/**
 * Helper for observing [IPProtectionState] and dispatching menu state updates.
 *
 * @param ipProtectionStore The store to observe for proxy status changes.
 * @param menuStore The [Store] for holding the [MenuState] and applying [MenuAction]s.
 * @param mainDispatcher The [CoroutineDispatcher] for state observation.
 */
class IPProtectionMenuBinding(
    ipProtectionStore: IPProtectionStore,
    private val menuStore: MenuStore,
    mainDispatcher: CoroutineDispatcher = Dispatchers.Main,
) : AbstractBinding<IPProtectionState>(ipProtectionStore, mainDispatcher) {

    override suspend fun onState(flow: Flow<IPProtectionState>) {
        flow
            .map { it.toMenuState() }
            .distinctUntilChanged()
            .collect {
                menuStore.dispatch(
                    MenuAction.UpdateIPProtectionMenuState(it),
                )
            }
    }

    private fun IPProtectionState.toMenuState() = IPProtectionMenuState(
        status = proxyStatus.toMenuStatus(),
        dataLimitGb = if (dataMaxBytes > 0) (dataMaxBytes / BYTES_PER_GB).toInt() else -1,
    )

    private fun ProxyStatus.toMenuStatus() = when (this) {
        is Uninitialized,
        is Authorized.Idle,
            -> IPProtectionMenuStatus.Disabled

        is Authorized.Activating -> IPProtectionMenuStatus.Activating
        is Authorized.Active -> IPProtectionMenuStatus.Enabled
        is Authorized.DataLimitReached -> IPProtectionMenuStatus.DataLimitReached
        is Authorized.ConnectionError -> IPProtectionMenuStatus.ConnectionError

        AuthenticationRequired,
        AuthorizationRequired,
            -> IPProtectionMenuStatus.AuthRequired
    }
}
