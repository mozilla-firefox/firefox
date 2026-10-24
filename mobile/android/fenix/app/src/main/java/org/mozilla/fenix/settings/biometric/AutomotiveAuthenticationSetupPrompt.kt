/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.settings.biometric

import android.app.Activity
import android.content.ActivityNotFoundException
import android.content.Intent
import android.provider.Settings
import androidx.appcompat.app.AlertDialog
import com.google.android.material.dialog.MaterialAlertDialogBuilder
import mozilla.components.ui.widgets.withCenterAlignedButtons
import org.mozilla.fenix.R
import org.mozilla.fenix.ext.secure

/**
 * Prompt asking the user to set up a device lock before accessing sensitive data on Android Automotive OS.
 *
 * Unlike the other device lock warnings, the user cannot continue without setting up a lock, and no "already shown"
 * state is stored, so the prompt can be shown every time.
 */
interface AutomotiveAuthenticationSetupPrompt {
    /**
     * Shows the prompt.
     *
     * @param activity The activity used to show the prompt and open the device security settings.
     * @param onSetUp Invoked once the device security settings have been opened. The caller is responsible for checking
     *   whether a device lock was set up when the user comes back.
     * @param onCancel Invoked when the user cancels or dismisses the prompt, or when the device security settings could
     *   not be opened. The action that required authentication must not proceed.
     */
    fun show(
        activity: Activity,
        onSetUp: () -> Unit,
        onCancel: () -> Unit,
    ): AlertDialog
}

/** Default implementation of [AutomotiveAuthenticationSetupPrompt]. */
object DefaultAutomotiveAuthenticationSetupPrompt : AutomotiveAuthenticationSetupPrompt {
    override fun show(
        activity: Activity,
        onSetUp: () -> Unit,
        onCancel: () -> Unit,
    ): AlertDialog =
        MaterialAlertDialogBuilder(activity)
            .setTitle(R.string.automotive_authentication_setup_dialog_title)
            .setMessage(R.string.automotive_authentication_setup_dialog_message)
            .setNegativeButton(R.string.automotive_authentication_setup_dialog_cancel) { _, _ ->
                onCancel()
            }
            .setPositiveButton(R.string.automotive_authentication_setup_dialog_set_up_now) { _, _ ->
                try {
                    activity.startActivity(Intent(Settings.ACTION_SECURITY_SETTINGS))
                    onSetUp()
                } catch (_: ActivityNotFoundException) {
                    onCancel()
                }
            }
            .setOnCancelListener { onCancel() }
            .show()
            .withCenterAlignedButtons()
            .apply { secure(activity) }
}
