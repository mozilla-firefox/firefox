/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.settings.ipprotection

import mozilla.components.feature.ipprotection.store.state.IPProtectionState

/** Current VPN facts exposed under Nimbus's `triggerContext` */
internal fun IPProtectionState.toMessageContext(): Map<String, Any?> {
    return mapOf("vpn" to emptyMap<String, Any?>())
}
