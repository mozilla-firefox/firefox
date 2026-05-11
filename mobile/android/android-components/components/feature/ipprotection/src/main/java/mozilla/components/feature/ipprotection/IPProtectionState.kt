/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.ipprotection

import mozilla.components.lib.state.State

/**
 * State stored by the feature to drive UI and decisions.
 *
 * @property proxyStatus The proxy state.
 * @property dataRemainingBytes Remaining monthly data allowance in bytes.
 * @property dataMaxBytes Maximum monthly data allowance in bytes.
 * @property dataRemainingBytes Remaining monthly data allowance in bytes, or -1 if unavailable.
 * @property dataMaxBytes Maximum monthly data allowance in bytes, or -1 if unavailable.
 * @property resetDate ISO 8601 string for when the monthly allowance resets, or null if unavailable.
 * @property isSignedIn Whether the user is signed in.
 */
data class IPProtectionState(
    val eligibilityStatus: EligibilityStatus = EligibilityStatus.Unknown,
    val proxyStatus: ProxyStatus = Uninitialized,
    val dataRemainingBytes: Long = -1L,
    val dataMaxBytes: Long = -1L,
    val resetDate: String? = null,
    val isSignedIn: Boolean = false,
) : State {
    val isEligible: Boolean
        get() = eligibilityStatus == EligibilityStatus.Eligible
}
