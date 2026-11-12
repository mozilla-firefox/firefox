/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.components.accounts

import android.content.Context
import io.mockk.mockk
import io.mockk.verifyOrder
import mozilla.components.feature.accounts.FirefoxAccountsAuthFeature
import org.junit.Test
import org.mozilla.fenix.utils.Settings

class EntrypointTrackingAuthFeatureTest {

    private val context: Context = mockk()
    private val feature: FirefoxAccountsAuthFeature = mockk(relaxed = true)
    private val settings: Settings = mockk(relaxed = true)
    private val authFeature = EntrypointTrackingAuthFeature(feature, settings)

    @Test
    fun `GIVEN an entrypoint WHEN beginning an authentication THEN the entrypoint is stored before the flow begins`() {
        authFeature.beginAuthentication(
            context = context,
            entrypoint = FenixFxAEntryPoint.NewUserOnboarding,
            service = "sync",
            scopes = setOf("profile"),
        )

        verifyOrder {
            settings.lastAuthFlowEntrypoint = "newuser-onboarding"
            feature.beginAuthentication(
                context = context,
                entrypoint = FenixFxAEntryPoint.NewUserOnboarding,
                service = "sync",
                scopes = setOf("profile"),
            )
        }
    }

    @Test
    fun `GIVEN an entrypoint WHEN beginning a pairing authentication THEN the entrypoint is stored before the flow begins`() {
        authFeature.beginPairingAuthentication(
            context = context,
            pairingUrl = "https://accounts.firefox.com/pair#channel_id=1",
            entrypoint = FenixFxAEntryPoint.SettingsMenu,
            service = "sync",
            scopes = setOf("profile"),
        )

        verifyOrder {
            settings.lastAuthFlowEntrypoint = "settings-menu"
            feature.beginPairingAuthentication(
                context = context,
                pairingUrl = "https://accounts.firefox.com/pair#channel_id=1",
                entrypoint = FenixFxAEntryPoint.SettingsMenu,
                service = "sync",
                scopes = setOf("profile"),
            )
        }
    }
}
