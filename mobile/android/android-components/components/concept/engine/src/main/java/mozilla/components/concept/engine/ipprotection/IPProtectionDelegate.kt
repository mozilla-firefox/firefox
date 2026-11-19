/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.concept.engine.ipprotection

import mozilla.components.ExperimentalAndroidComponentsApi
import mozilla.components.concept.engine.Engine

/** Engine-to-app callbacks for IP protection state changes. Passed to [Engine.registerIPProtectionDelegate]. */
@ExperimentalAndroidComponentsApi
interface IPProtectionDelegate {
    /**
     * Called when the IP protection service state changes.
     *
     * @param serviceState The current [ServiceState].
     */
    fun onServiceStateChanged(serviceState: ServiceState)

    /**
     * Called when the IP protection proxy state changes.
     *
     * @param proxyState The current [ProxyState].
     * @param lastError The error the proxy last failed with, or null.
     */
    fun onProxyStateChanged(proxyState: ProxyState, lastError: String?)

    /**
     * Called when the data usage changes.
     *
     * @param remaining Remaining data allowance in bytes.
     * @param max Maximum data allowance in bytes, or 0 if unlimited.
     * @param resetTime ISO 8601 string for when the allowance resets, or null if unavailable.
     */
    fun onUsageChanged(remaining: Long, max: Long, resetTime: String?)

    /**
     * Called when the list of available proxy countries changes, either in response to
     * [IPProtectionHandler.updateCountryList] or by a server side push.
     *
     * @param countries The current list of available [IPProtectionHandler.Country] entries.
     */
    fun onCountryListChanged(countries: List<IPProtectionHandler.Country>) = Unit
}
