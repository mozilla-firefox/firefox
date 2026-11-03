/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.settings.logins.ui

import android.app.Activity
import android.app.KeyguardManager
import android.content.DialogInterface
import android.content.Intent
import android.provider.Settings
import androidx.activity.result.ActivityResult
import androidx.activity.result.ActivityResultCallback
import androidx.activity.result.ActivityResultLauncher
import androidx.activity.result.contract.ActivityResultContracts
import androidx.biometric.BiometricManager
import androidx.biometric.BiometricManager.Authenticators.BIOMETRIC_WEAK
import androidx.biometric.BiometricManager.Authenticators.DEVICE_CREDENTIAL
import androidx.biometric.BiometricPrompt
import androidx.core.content.ContextCompat
import androidx.core.content.getSystemService
import androidx.fragment.app.FragmentActivity
import com.google.android.material.dialog.MaterialAlertDialogBuilder
import mozilla.components.ui.widgets.withCenterAlignedButtons
import org.mozilla.fenix.R
import org.mozilla.fenix.ext.components
import org.mozilla.fenix.ext.secure
import org.mozilla.fenix.settings.biometric.BiometricPromptFeature
import org.mozilla.fenix.settings.biometric.ext.isDeviceLockSetUp

/** Allows handling of biometric authentication with compose. */
interface BiometricAuthenticationUtils {
    /**
     * Checks if the appropriate SDK version and hardware capabilities are met to use the feature.
     *
     * @param activity The base activity.
     */
    fun canUseBiometricAuthentication(activity: FragmentActivity): Boolean

    /**
     * Prompts the biometric authentication dialog, adapted to be called from a composable context.
     *
     * @param title The title of the authentication prompt.
     * @param activity The base activity.
     * @param onAuthSuccess Callback triggered when biometric authentication succeeds.
     * @param onAuthFailure Callback triggered when biometric authentication fails.
     */
    fun showBiometricPromptForCompose(
        title: String,
        activity: FragmentActivity,
        onAuthSuccess: () -> Unit,
        onAuthFailure: () -> Unit,
    )

    /**
     * Checks if the user has a secure lock screen.
     *
     * @param activity The base activity.
     */
    fun canUsePinVerification(activity: FragmentActivity): Boolean

    /**
     * Prompts the pin verification dialog, adapted to be called from a composable context.
     *
     * @param title The title of the authentication prompt.
     * @param activity The base activity.
     * @param onAuthSuccess Callback triggered when pin verification succeeds.
     */
    fun showPinVerificationPrompt(
        title: String,
        activity: FragmentActivity,
        onAuthSuccess: () -> Unit,
    )

    /**
     * Prompts a warning dialog, in case the user hasn't set a secure lock.
     *
     * @param activity The base activity.
     * @param onAuthSuccess Callback triggered when the user chooses to set a pin or continue without one.
     */
    fun showPinWarningPrompt(activity: FragmentActivity, onAuthSuccess: () -> Unit)
}

/** Default implementation of [BiometricAuthenticationUtils]. */
object DefaultBiometricUtils : BiometricAuthenticationUtils {
    override fun canUseBiometricAuthentication(activity: FragmentActivity): Boolean =
        BiometricPromptFeature.canUseFeature(BiometricManager.from(activity))

    override fun showBiometricPromptForCompose(
        title: String,
        activity: FragmentActivity,
        onAuthSuccess: () -> Unit,
        onAuthFailure: () -> Unit,
    ) {
        val executor = ContextCompat.getMainExecutor(activity)

        val biometricPrompt =
            BiometricPrompt(
                activity,
                executor,
                object : BiometricPrompt.AuthenticationCallback() {
                    override fun onAuthenticationError(errorCode: Int, errString: CharSequence) {
                        super.onAuthenticationError(errorCode, errString)
                        onAuthFailure()
                    }

                    override fun onAuthenticationSucceeded(result: BiometricPrompt.AuthenticationResult) {
                        super.onAuthenticationSucceeded(result)
                        onAuthSuccess()
                    }

                    override fun onAuthenticationFailed() {
                        super.onAuthenticationFailed()
                        onAuthFailure()
                    }
                },
            )

        val promptInfo =
            BiometricPrompt.PromptInfo.Builder()
                .setTitle(title)
                .setAllowedAuthenticators(BIOMETRIC_WEAK or DEVICE_CREDENTIAL)
                .build()

        biometricPrompt.authenticate(promptInfo)
    }

    override fun canUsePinVerification(activity: FragmentActivity): Boolean = activity.isDeviceLockSetUp()

    /**
     * Builds an intent to be used for starting an activity that will display the pin verification dialog.
     *
     * @param title The title of the pin verification prompt.
     * @param activity The base activity.
     */
    @Suppress("Deprecation")
    fun getConfirmDeviceCredentialIntent(title: String, activity: FragmentActivity): Intent? {
        val manager = activity.getSystemService<KeyguardManager>()
        if (manager != null) {
            val confirmDeviceCredentialIntent =
                manager.createConfirmDeviceCredentialIntent(
                    activity.resources.getString(R.string.logins_biometric_prompt_message_pin),
                    title,
                )
            return confirmDeviceCredentialIntent
        } else {
            return null
        }
    }

    @Suppress("Deprecation")
    override fun showPinVerificationPrompt(
        title: String,
        activity: FragmentActivity,
        onAuthSuccess: () -> Unit,
    ) {
        val manager = activity.getSystemService<KeyguardManager>()
        if (manager != null) {
            val confirmDeviceCredentialIntent =
                manager.createConfirmDeviceCredentialIntent(
                    activity.resources.getString(R.string.logins_biometric_prompt_message_pin),
                    title,
                )

            val startForResult: ActivityResultLauncher<Intent> =
                activity.registerForActivityResult(
                    ActivityResultContracts.StartActivityForResult(),
                    object : ActivityResultCallback<ActivityResult> {
                        override fun onActivityResult(result: ActivityResult) {
                            if (result.resultCode == Activity.RESULT_OK) {
                                onAuthSuccess()
                            }
                        }
                    },
                )
            startForResult.launch(confirmDeviceCredentialIntent)
        }
    }

    override fun showPinWarningPrompt(
        activity: FragmentActivity,
        onAuthSuccess: () -> Unit,
    ) {
        if (activity.components.settings.shouldShowSecurityPinWarning) {
            showPinDialogWarning(activity, onAuthSuccess)
        } else {
            onAuthSuccess()
        }
    }
}

private fun showPinDialogWarning(
    activity: FragmentActivity,
    onIgnorePinWarning: () -> Unit,
) {
    MaterialAlertDialogBuilder(activity)
        .apply {
            setTitle(context.resources.getString(R.string.logins_warning_dialog_title_2))
            setMessage(context.resources.getString(R.string.logins_warning_dialog_message_2))

            setNegativeButton(context.resources.getString(R.string.logins_warning_dialog_later)) { _: DialogInterface, _
                ->
                onIgnorePinWarning()
            }

            setPositiveButton(context.resources.getString(R.string.logins_warning_dialog_set_up_now)) {
                it: DialogInterface,
                _ ->
                it.dismiss()
                val intent = Intent(Settings.ACTION_SECURITY_SETTINGS)
                context.startActivity(intent)
            }
            create().withCenterAlignedButtons()
        }
        .show()
        .secure(activity)
    activity.components.settings.incrementSecureWarningCount()
}
