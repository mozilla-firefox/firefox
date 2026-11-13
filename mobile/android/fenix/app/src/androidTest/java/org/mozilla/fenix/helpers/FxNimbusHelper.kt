/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.helpers

import android.util.Log
import org.mozilla.fenix.helpers.Constants.TAG
import org.mozilla.fenix.nimbus.FxNimbus
import org.mozilla.fenix.nimbus.IpProtection
import org.mozilla.fenix.nimbus.Translations

object FxNimbusHelper {
    /** Disable the translations prompt after a page that can be translated is loaded. */
    fun disablePageLoadTranslationsPrompt() {
        Log.i(TAG, "disableTranslationsPrompt: Trying to disable the translations prompt")
        FxNimbus.features.translations.withInitializer { _, _ ->
            Translations(mainFlowToolbarEnabled = false)
        }
        Log.i(TAG, "disableTranslationsPrompt: Disabled the translations prompt")
    }

    /** Enable the translations prompt after a page that can be translated is loaded. */
    fun enablePageLoadTranslationsPrompt() {
        Log.i(TAG, "enableTranslationsPrompt: Trying to enable the translations prompt")
        FxNimbus.features.translations.withInitializer { _, _ ->
            Translations(mainFlowToolbarEnabled = true)
        }
        Log.i(TAG, "enableTranslationsPrompt: Enabled the translations prompt")
    }

    /** Disable the IP Protection feature. */
    fun disableIPProtection() {
        Log.i(TAG, "disableIPProtection: Trying to disable the IP Protection feature")
        FxNimbus.features.ipProtection.withInitializer { _, _ ->
            IpProtection(enabled = false)
        }
        Log.i(TAG, "disableIPProtection: Disabled the IP Protection feature")
    }

    /** Enable the IP Protection feature. */
    fun enableIPProtection() {
        Log.i(TAG, "enableIPProtection: Trying to enable the IP Protection feature")
        FxNimbus.features.ipProtection.withInitializer { _, _ ->
            IpProtection(enabled = true)
        }
        Log.i(TAG, "enableIPProtection: Enabled the IP Protection feature")
    }
}
