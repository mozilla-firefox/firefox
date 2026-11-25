/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.settings.ipprotection

import mozilla.components.ExperimentalAndroidComponentsApi
import mozilla.components.concept.engine.ipprotection.CurrentAuthProvider
import mozilla.components.feature.ipprotection.store.state.IPProtectionState
import mozilla.components.feature.ipprotection.store.state.IPProtectionStatus
import mozilla.components.feature.ipprotection.store.state.ipProtectionStatus
import org.mozilla.fenix.settings.isReadyToUse

/** Current VPN facts exposed under Nimbus's `triggerContext` */
@OptIn(ExperimentalAndroidComponentsApi::class)
internal fun IPProtectionState.toMessageContext(): Map<String, Any?> {
    return mapOf(
        "vpn" to
            mapOf(
                "auth_provider" to
                    when (currentAuthProvider) {
                        CurrentAuthProvider.Unknown -> "unknown"
                        CurrentAuthProvider.Fxa -> "fxa"
                        CurrentAuthProvider.Gpi -> "gpi"
                    },
                "is_ready_to_use" to ipProtectionStatus.isReadyToUse(),
                "is_enabled" to (ipProtectionStatus == IPProtectionStatus.Enabled),
                "usage_reset_prompt_pending" to usageQuotaResetPromptPending,
            )
    )
}
