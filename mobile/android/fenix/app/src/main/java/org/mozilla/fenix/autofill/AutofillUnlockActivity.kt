/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.autofill

import androidx.annotation.VisibleForTesting
import mozilla.components.feature.autofill.AutofillConfiguration
import mozilla.components.feature.autofill.R as autofillR
import mozilla.components.feature.autofill.ui.AbstractAutofillUnlockActivity
import org.mozilla.fenix.ext.components
import org.mozilla.fenix.settings.biometric.AutomotiveSensitiveDataAuthenticator
import org.mozilla.fenix.settings.biometric.DefaultAutomotiveSensitiveDataAuthenticator

/** Activity responsible for unlocking the autofill service by asking the user to verify. */
class AutofillUnlockActivity : AbstractAutofillUnlockActivity() {
    override val configuration: AutofillConfiguration by lazy { components.autofillConfiguration }

    @VisibleForTesting
    internal var sensitiveDataAuthenticator: AutomotiveSensitiveDataAuthenticator =
        DefaultAutomotiveSensitiveDataAuthenticator()

    public override fun authenticate(onSuccess: () -> Unit, onFailure: () -> Unit): Boolean {
        if (!sensitiveDataAuthenticator.isRequired(this)) {
            return false
        }

        sensitiveDataAuthenticator.authenticate(
            activity = this,
            title =
                getString(
                    autofillR.string.mozac_feature_autofill_popup_unlock_application,
                    configuration.applicationName,
                ),
            onAuthenticated = onSuccess,
            onBlocked = onFailure,
        )
        return true
    }
}
