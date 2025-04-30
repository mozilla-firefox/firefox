/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.settings.biometric

import android.app.KeyguardManager
import android.content.Context
import android.content.Intent
import android.os.Bundle
import android.view.View
import androidx.activity.result.ActivityResultLauncher
import androidx.biometric.BiometricManager
import androidx.core.content.getSystemService
import androidx.preference.Preference
import androidx.preference.PreferenceFragmentCompat
import mozilla.components.support.base.feature.ViewBoundFeatureWrapper
import org.mozilla.fenix.ext.registerForActivityResult
import org.mozilla.fenix.ext.settings
import org.mozilla.fenix.settings.requirePreference

/**
 * Helper for creating and implementing the [BiometricPromptFeature]. Currently used
 * for logins and credit cards.
 */
abstract class BiometricPromptPreferenceFragment : PreferenceFragmentCompat() {

    private val biometricPromptFeature = ViewBoundFeatureWrapper<BiometricPromptFeature>()
    internal lateinit var startForResult: ActivityResultLauncher<Intent>

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        startForResult = registerForActivityResult {
            navigateOnSuccess()
        }
    }

    /**
     * Gets the string to be used for [BiometricPromptFeature.requestAuthentication] prompting to
     * unlock the device.
     */
    abstract fun unlockMessage(): String

    /**
     * Navigate when authentication is successful.
     */
    abstract fun navigateOnSuccess()

    /**
     * Shows a dialog warning to set up a pin/password when the device is not secured. This is
     * only used when BiometricPrompt is unavailable on the device.
     */
    abstract fun showPinDialogWarning(context: Context)

    /**
     * Toggle preferences to enable or disable navigation during authentication flows.
     *
     * @param prefList a list of [Preference]s to toggle.
     * @param enabled whether or not the preferences should be enabled.
     */
    fun togglePrefsEnabled(prefList: List<Int>, enabled: Boolean) {
        for (preference in prefList) {
            requirePreference<Preference>(preference).isEnabled = enabled
        }
    }

    /**
     * Creates a prompt to verify the device's pin/password and start activity based on the result.
     * This is only used when BiometricPrompt is unavailable on the device.
     */
    @Suppress("Deprecation")
    abstract fun showPinVerification(manager: KeyguardManager)

    /**
     * Sets the biometric prompt feature.
     *
     * @param view the view that the prompt will be associate with.
     * @param prefList a list of [Preference]s to toggle.
     */
    fun setBiometricPrompt(view: View, prefList: List<Int>) {
        biometricPromptFeature.set(
            feature = BiometricPromptFeature(
                context = requireContext(),
                fragment = this,
                onAuthFailure = {
                    togglePrefsEnabled(prefList, true)
                },
                onAuthSuccess = ::navigateOnSuccess,
            ),
            owner = this,
            view = view,
        )
    }

    /**
     * Use [BiometricPromptFeature] or [KeyguardManager] to confirm device security.
     *
     * @param context An Android [Context].
     * @param prefList a list of [Preference]s to disable while authentication is happening.
     */
    fun verifyCredentialsOrShowSetupWarning(context: Context, prefList: List<Int>) {
        // Use the BiometricPrompt if available
        if (BiometricPromptFeature.canUseFeature(BiometricManager.from(context))) {
            togglePrefsEnabled(prefList, false)
            biometricPromptFeature.get()?.requestAuthentication(unlockMessage())
            return
        }

        // Fallback to prompting for password with the KeyguardManager
        val manager = context.getSystemService<KeyguardManager>()
        if (manager?.isKeyguardSecure == true) {
            showPinVerification(manager)
        } else {
            // Warn that the device has not been secured
            if (context.settings().shouldShowSecurityPinWarning) {
                showPinDialogWarning(context)
            } else {
                navigateOnSuccess()
            }
        }
    }
}
