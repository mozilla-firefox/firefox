/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.components.accounts

import android.content.Context
import mozilla.components.concept.sync.FxAEntryPoint
import mozilla.components.feature.accounts.FirefoxAccountsAuthFeature
import org.mozilla.fenix.utils.Settings

/**
 * Begins authentication flows through [feature] and stores the entrypoint of the latest flow in
 * [Settings.lastAuthFlowEntrypoint] so that the sign-in can be attributed to it.
 *
 * @param feature The [FirefoxAccountsAuthFeature] that begins the authentication flows.
 * @param settings The [Settings] in which the entrypoint is stored.
 */
class EntrypointTrackingAuthFeature(
    private val feature: FirefoxAccountsAuthFeature,
    private val settings: Settings,
) {
    /** @see FirefoxAccountsAuthFeature.beginAuthentication */
    fun beginAuthentication(
        context: Context,
        entrypoint: FxAEntryPoint,
        service: String,
        scopes: Set<String> = emptySet(),
    ) {
        settings.lastAuthFlowEntrypoint = entrypoint.entryName
        feature.beginAuthentication(context, entrypoint, service, scopes)
    }

    /** @see FirefoxAccountsAuthFeature.beginPairingAuthentication */
    fun beginPairingAuthentication(
        context: Context,
        pairingUrl: String,
        entrypoint: FxAEntryPoint,
        service: String,
        scopes: Set<String> = emptySet(),
    ) {
        settings.lastAuthFlowEntrypoint = entrypoint.entryName
        feature.beginPairingAuthentication(context, pairingUrl, entrypoint, service, scopes)
    }
}
