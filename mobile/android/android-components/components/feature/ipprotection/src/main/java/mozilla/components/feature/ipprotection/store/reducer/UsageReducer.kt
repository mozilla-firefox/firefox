/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

@file:OptIn(ExperimentalAndroidComponentsApi::class)

package mozilla.components.feature.ipprotection.store.reducer

import kotlin.time.Instant
import mozilla.components.ExperimentalAndroidComponentsApi
import mozilla.components.feature.ipprotection.store.IPProtectionAction
import mozilla.components.feature.ipprotection.store.state.IPProtectionState
import mozilla.components.feature.ipprotection.store.state.UsageQuota
import mozilla.components.feature.ipprotection.store.state.asDataVolume

internal object UsageReducer {
    fun reduce(
        state: IPProtectionState,
        action: IPProtectionAction.UsageAction,
    ): IPProtectionState =
        when (action) {
            is IPProtectionAction.UsageAction.UsageChanged -> {
                state.copy(usageQuota = action.toUsageQuota())
            }
        }
}

/**
 * Converts the data usage items in [UsageChanged] to [UsageQuota]. When either [remaining] or [max] is negative, the
 * quota information is unavailable.
 */
private fun IPProtectionAction.UsageAction.UsageChanged.toUsageQuota(): UsageQuota? {
    val remainingVolume = remaining.takeIf { it >= 0 }?.asDataVolume()
    val maxVolume = max.takeIf { it > 0 }?.asDataVolume()

    val reset = resetTime?.let(Instant::parseOrNull)

    return when {
        remainingVolume == null -> null
        max == 0L -> UsageQuota.Unmetered(reset)
        maxVolume == null -> null
        else -> UsageQuota.Metered(resetTime = reset, max = maxVolume, remaining = remainingVolume)
    }
}
