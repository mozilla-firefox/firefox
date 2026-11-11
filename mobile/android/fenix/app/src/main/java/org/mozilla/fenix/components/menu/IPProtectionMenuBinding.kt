/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.components.menu

import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.distinctUntilChanged
import kotlinx.coroutines.flow.map
import mozilla.components.feature.ipprotection.store.IPProtectionStore
import mozilla.components.feature.ipprotection.store.state.IPProtectionState
import mozilla.components.feature.ipprotection.store.state.UsageQuota
import mozilla.components.feature.ipprotection.store.state.ipProtectionStatus
import mozilla.components.lib.state.helpers.AbstractBinding
import org.mozilla.fenix.components.menu.store.IPProtectionMenuState

/**
 * Helper for observing [IPProtectionState] and dispatching menu state updates.
 *
 * @param ipProtectionStore The store to observe for proxy status changes.
 * @param onIPProtectionStatusUpdate Invoked when the IP protection status is updated.
 * @param mainDispatcher The [CoroutineDispatcher] for state observation.
 */
class IPProtectionMenuBinding(
    ipProtectionStore: IPProtectionStore,
    private val onIPProtectionStatusUpdate: (IPProtectionMenuState) -> Unit,
    mainDispatcher: CoroutineDispatcher = Dispatchers.Main,
) : AbstractBinding<IPProtectionState>(ipProtectionStore, mainDispatcher) {

    override suspend fun onState(flow: Flow<IPProtectionState>) {
        flow
            .map { it.toMenuState() }
            .distinctUntilChanged()
            .collect {
                onIPProtectionStatusUpdate(it)
            }
    }
}

/** Maps this [IPProtectionState] to the [IPProtectionMenuState] used to configure the menu item. */
internal fun IPProtectionState.toMenuState() =
    IPProtectionMenuState(
        status = ipProtectionStatus,
        dataLimit = (usageQuota as? UsageQuota.Metered)?.max,
    )
