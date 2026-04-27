/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.autofill

import android.content.Context
import android.content.Intent
import android.provider.Settings
import android.view.autofill.AutofillManager
import androidx.core.net.toUri
import mozilla.components.support.base.log.logger.Logger

/**
 * Use cases for common Android Autofill tasks.
 */
class AutofillUseCases {
    private val logger = Logger("AutofillUseCases")

    /**
     * Returns true if Autofill is supported by the current device, defaulting to false in the case
     * of an exception.
     */
    fun isSupported(context: Context): Boolean = Result.runCatching {
        context.getSystemService(AutofillManager::class.java).isAutofillSupported
    }.getOrDefault(false)

    /**
     * Returns true if this application is providing Autofill services for the current user.
     */
    @Suppress("TooGenericExceptionCaught")
    fun isEnabled(context: Context): Boolean {
        return try {
            context.getSystemService(AutofillManager::class.java)
                .hasEnabledAutofillServices()
        } catch (e: RuntimeException) {
            // Without more detail about why the system service has timed out, it's easiest to assume
            // that the failure will continue and so disable the service for now.
            logger.error("System service lookup has timed out")
            false
        }
    }

    /**
     * Opens the system's autofill settings to let the user select an autofill service.
     */
    fun enable(context: Context) {
        val intent = Intent(Settings.ACTION_REQUEST_SET_AUTOFILL_SERVICE)
        intent.data = "package:${context.packageName}".toUri()
        context.startActivity(intent)
    }

    /**
     * Disables autofill if this application is providing Autofill services for the current user.
     */
    fun disable(context: Context) {
        context.getSystemService(AutofillManager::class.java)
            .disableAutofillServices()
    }
}
