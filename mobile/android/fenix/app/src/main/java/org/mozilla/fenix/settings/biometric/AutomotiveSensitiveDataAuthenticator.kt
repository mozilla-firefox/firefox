/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.settings.biometric

import android.app.KeyguardManager
import android.content.Context
import androidx.annotation.VisibleForTesting
import androidx.biometric.BiometricManager.Authenticators.BIOMETRIC_WEAK
import androidx.biometric.BiometricManager.Authenticators.DEVICE_CREDENTIAL
import androidx.biometric.BiometricPrompt
import androidx.core.content.ContextCompat
import androidx.core.content.getSystemService
import androidx.fragment.app.FragmentActivity
import mozilla.components.feature.automotive.isAndroidAutomotiveAvailable
import org.mozilla.fenix.nimbus.FxNimbus

/**
 * Authentication check that every flow accessing sensitive data (passwords, credit cards, Mozilla account sign-in)
 * must go through on Android Automotive OS before continuing.
 */
interface AutomotiveSensitiveDataAuthenticator {
    /**
     * Asks the user to authenticate with their device lock, first prompting them to set one up if needed. Continues
     * right away when the check does not apply, i.e. when not on Android Automotive OS or when the
     * `automotive-sensitive-data` Nimbus feature is disabled. No state is kept between calls.
     *
     * @param activity The activity used to show the prompts.
     * @param title The title of the authentication prompt.
     * @param onAuthenticated Invoked when the action that required authentication can proceed.
     * @param onBlocked Invoked when the user cancelled the setup prompt, did not set up a device lock, or failed or
     *   cancelled authentication. The action that required authentication must not proceed.
     */
    fun authenticate(
        activity: FragmentActivity,
        title: String,
        onAuthenticated: () -> Unit,
        onBlocked: () -> Unit,
    )
}

/**
 * Default implementation of [AutomotiveSensitiveDataAuthenticator].
 *
 * @param isRequired Whether the authentication check applies on this device.
 * @param isDeviceLockSetUp Whether the user has a device lock pattern, PIN, or password set up.
 * @param setupPrompt The prompt asking the user to set up a device lock.
 * @param showAuthenticationPrompt Shows the authentication prompt, invoking exactly one of its callbacks.
 */
class DefaultAutomotiveSensitiveDataAuthenticator(
    private val isRequired: (Context) -> Boolean = ::isAutomotiveSensitiveDataAuthenticationRequired,
    private val isDeviceLockSetUp: (Context) -> Boolean = { it.getSystemService<KeyguardManager>()?.isKeyguardSecure == true },
    private val setupPrompt: AutomotiveAuthenticationSetupPrompt = DefaultAutomotiveAuthenticationSetupPrompt,
    private val showAuthenticationPrompt: (
        activity: FragmentActivity,
        title: String,
        onSuccess: () -> Unit,
        onFailure: () -> Unit,
    ) -> Unit = ::showDeviceLockPrompt,
) : AutomotiveSensitiveDataAuthenticator {
    override fun authenticate(
        activity: FragmentActivity,
        title: String,
        onAuthenticated: () -> Unit,
        onBlocked: () -> Unit,
    ) {
        when {
            !isRequired(activity) -> onAuthenticated()
            isDeviceLockSetUp(activity) -> showAuthenticationPrompt(activity, title, onAuthenticated, onBlocked)
            else ->
                setupPrompt.show(
                    activity = activity,
                    onSetUp = { showAuthenticationPrompt(activity, title, onAuthenticated, onBlocked) },
                    onCancel = onBlocked,
                )
        }
    }
}

@VisibleForTesting
internal fun isAutomotiveSensitiveDataAuthenticationRequired(context: Context): Boolean =
    context.isAndroidAutomotiveAvailable() && FxNimbus.features.automotiveSensitiveData.value().enabled

private fun showDeviceLockPrompt(
    activity: FragmentActivity,
    title: String,
    onSuccess: () -> Unit,
    onFailure: () -> Unit,
) {
    val promptInfo =
        BiometricPrompt.PromptInfo.Builder()
            .setTitle(title)
            .setAllowedAuthenticators(BIOMETRIC_WEAK or DEVICE_CREDENTIAL)
            .build()

    BiometricPrompt(
        activity,
        ContextCompat.getMainExecutor(activity),
        DeviceLockAuthenticationCallback(onSuccess, onFailure),
    ).authenticate(promptInfo)
}

/**
 * Only forwards the final result of the authentication. [onAuthenticationFailed] is not final, as the prompt stays
 * open and the user can try again.
 */
@VisibleForTesting
internal class DeviceLockAuthenticationCallback(
    private val onSuccess: () -> Unit,
    private val onFailure: () -> Unit,
) : BiometricPrompt.AuthenticationCallback() {
    override fun onAuthenticationSucceeded(result: BiometricPrompt.AuthenticationResult) = onSuccess()

    override fun onAuthenticationError(errorCode: Int, errString: CharSequence) = onFailure()
}
