/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.settings.account

import mozilla.components.concept.sync.AccountObserver
import mozilla.components.concept.sync.AuthType
import mozilla.components.concept.sync.OAuthAccount
import mozilla.components.feature.ipprotection.auth.fxa.IPProtectionActivityAuthFlowObserver
import mozilla.components.feature.ipprotection.auth.fxa.IPProtectionFxaAuthFlow.Companion.INTENT_ON_COMPLETE
import org.mozilla.fenix.customtabs.ExternalAppBrowserActivity
import org.mozilla.fenix.ext.components

/** A special custom tab for signing in to a Firefox Account. The activity is closed once the user is signed in. */
class AuthCustomTabActivity : ExternalAppBrowserActivity() {

    private val ipProtectionActivityAuthFlowObserver =
        IPProtectionActivityAuthFlowObserver(
            lazy { components.ipProtection.store },
            lazy { intent.getBooleanExtra(INTENT_ON_COMPLETE, false) },
        )

    private val accountStateObserver =
        object : AccountObserver {
            /** Navigate away from this activity when we have successful authentication */
            override fun onAuthenticated(account: OAuthAccount, authType: AuthType) {
                // N.B: This is no where close to perfect because we need to know when authentication is complete for
                // our
                // specific scope, but we don't have this capability today.
                // We've tried doing this in the `FxaAccountStoreSync` but because our AuthCustomTabActivity extends
                // from
                // `HomeActivity` those observes experience an "Authentication" event immediately that resets the state
                // machine.
                finish()
            }
        }

    override fun onResume() {
        super.onResume()
        val accountManager = components.backgroundServices.accountManager
        accountManager.apply {
            register(ipProtectionActivityAuthFlowObserver, this@AuthCustomTabActivity, true)
            register(accountStateObserver, this@AuthCustomTabActivity, true)
        }
        lifecycle.addObserver(ipProtectionActivityAuthFlowObserver)
    }

    override fun onDestroy() {
        // Manually unregister here because we call `Activity#finish` in the observer
        // which then leaks
        components.backgroundServices.accountManager.unregister(accountStateObserver)

        super.onDestroy()
    }
}
