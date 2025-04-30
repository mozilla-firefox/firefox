/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.settings.biometric

import android.app.KeyguardManager
import android.content.DialogInterface
import android.content.Intent
import android.provider.Settings
import android.view.View
import androidx.annotation.StringRes
import androidx.appcompat.app.AlertDialog
import androidx.biometric.BiometricManager
import androidx.core.content.getSystemService
import androidx.fragment.app.Fragment
import androidx.fragment.app.FragmentActivity
import androidx.fragment.app.findFragment
import androidx.lifecycle.lifecycleScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import mozilla.components.support.base.feature.ViewBoundFeatureWrapper
import mozilla.components.ui.widgets.withCenterAlignedButtons
import org.mozilla.fenix.R
import org.mozilla.fenix.ext.runIfFragmentIsAttached
import org.mozilla.fenix.ext.secure
import org.mozilla.fenix.ext.settings

/**
 * Prompts the biometric authentication before navigating to new fragment
 * or displays warning dialog in case the feature is not available
 */
@Suppress("Deprecation")
fun bindBiometricsCredentialsPromptOrShowWarning(
    @StringRes titleRes: Int = R.string.logins_biometric_prompt_message_2,
    view: View,
    onShowPinVerification: (Intent) -> Unit,
    onAuthSuccess: () -> Unit,
    onAuthFailure: () -> Unit,
) {
    val (fragment, context) = Result.runCatching {
        view.findFragment() as Fragment to view.context
    }.getOrElse { return }

    val biometricPromptFeature = ViewBoundFeatureWrapper(
        owner = fragment.viewLifecycleOwner,
        view = view,
        feature = BiometricPromptFeature(
            context = context,
            fragment = fragment,
            onAuthSuccess = {
                fragment.runIfFragmentIsAttached {
                    fragment.lifecycleScope.launch(Dispatchers.Main) {
                        onAuthSuccess()
                    }
                }
            },
            onAuthFailure = {
                fragment.runIfFragmentIsAttached {
                    fragment.lifecycleScope.launch(Dispatchers.Main) {
                        onAuthFailure()
                    }
                }
            },
        ),
    )
    // Use the BiometricPrompt first
    if (BiometricPromptFeature.canUseFeature(BiometricManager.from(context))) {
        biometricPromptFeature.get()
            ?.requestAuthentication(context.resources.getString(titleRes))
        return
    }

    // Fallback to prompting for password with the KeyguardManager
    val manager = context.getSystemService<KeyguardManager>()
    if (manager?.isKeyguardSecure == true) {
        val confirmDeviceCredentialIntent = manager.createConfirmDeviceCredentialIntent(
            context.resources.getString(R.string.logins_biometric_prompt_message_pin),
            context.resources.getString(titleRes),
        )
        onShowPinVerification(confirmDeviceCredentialIntent)
    } else {
        // Warn that the device has not been secured
        if (context.settings().shouldShowSecurityPinWarning) {
            fragment.activity?.let {
                showPinDialogWarning(it, onAuthSuccess)
            } ?: return
        } else {
            onAuthSuccess()
        }
    }
}

private fun showPinDialogWarning(
    activity: FragmentActivity,
    onIgnorePinWarning: () -> Unit,
) {
    AlertDialog.Builder(activity).apply {
        setTitle(context.resources.getString(R.string.logins_warning_dialog_title_2))
        setMessage(
            context.resources.getString(R.string.logins_warning_dialog_message_2),
        )

        setNegativeButton(
            context.resources.getString(R.string.logins_warning_dialog_later),
        ) { _: DialogInterface, _ ->
            onIgnorePinWarning()
        }

        setPositiveButton(
            context.resources.getString(R.string.logins_warning_dialog_set_up_now),
        ) { it: DialogInterface, _ ->
            it.dismiss()
            val intent = Intent(Settings.ACTION_SECURITY_SETTINGS)
            context.startActivity(intent)
        }
        create().withCenterAlignedButtons()
    }.show().secure(activity)
    activity.settings().incrementSecureWarningCount()
}
