/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.components.menu.store

import androidx.compose.runtime.Immutable
import mozilla.components.feature.ipprotection.store.state.DataVolume
import mozilla.components.feature.ipprotection.store.state.IPProtectionStatus

/**
 * Represents the state of the IP protection menu item.
 *
 * @property status The current [IPProtectionStatus] shown in the badge.
 * @property dataLimit The limit of the [mozilla.components.feature.ipprotection.store.state.UsageQuota.Metered]
 *   [DataVolume] quota. When the quota is unknown or unlimited, this value is null.
 */
@Immutable
data class IPProtectionMenuState(
    val status: IPProtectionStatus = IPProtectionStatus.Uninitialized,
    val dataLimit: DataVolume? = null,
)
