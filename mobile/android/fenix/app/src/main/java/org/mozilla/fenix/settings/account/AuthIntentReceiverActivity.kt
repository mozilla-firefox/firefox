/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.settings.account

import android.app.Activity
import android.content.Intent
import android.os.Bundle
import androidx.annotation.VisibleForTesting
import kotlinx.coroutines.MainScope
import kotlinx.coroutines.launch
import mozilla.components.feature.ipprotection.auth.fxa.IPProtectionFxaAuthFlow.Companion.INTENT_ON_COMPLETE
import org.mozilla.fenix.HomeActivity
import org.mozilla.fenix.ext.components

/** Processes incoming intents and sends them to the corresponding activity. */
class AuthIntentReceiverActivity : Activity() {

    @VisibleForTesting
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        MainScope().launch {
            // The intent property is nullable, but the rest of the code below
            // assumes it is not. If it's null, then we make a new one and open
            // the HomeActivity.
            val intent = intent?.let { Intent(intent) } ?: Intent()

            if (components.settings.lastKnownMode.isPrivate) {
                components.intentProcessors.privateAuthCustomTabIntentProcessor.process(intent)
            } else {
                components.intentProcessors.authCustomTabIntentProcessor.process(intent)
            }

            intent.setClassName(applicationContext, AuthCustomTabActivity::class.java.name)
            intent.putExtra(HomeActivity.OPEN_TO_BROWSER, true)

            // Forward the intent onward for the account manager to handle it.
            intent.putExtra(INTENT_ON_COMPLETE, intent.getBooleanExtra(INTENT_ON_COMPLETE, false))

            startActivity(intent)

            finish()
        }
    }
}
