/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.settings.biometric

import android.content.ActivityNotFoundException
import android.content.Intent
import android.provider.Settings
import androidx.appcompat.app.AlertDialog
import androidx.fragment.app.FragmentActivity
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.LifecycleEventObserver
import androidx.lifecycle.LifecycleOwner
import com.google.android.material.dialog.MaterialAlertDialogBuilder
import mozilla.components.ui.widgets.withCenterAlignedButtons
import org.mozilla.fenix.R
import org.mozilla.fenix.ext.secure
import org.mozilla.fenix.settings.biometric.ext.isDeviceLockSetUp

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
     * @param onSetUp Invoked when the user comes back from the device security settings with a device lock set up.
     * @param onCancel Invoked when the user cancels or dismisses the prompt, when the device security settings could
     *   not be opened, or when the user comes back from them without a device lock set up. The action that required
     *   authentication must not proceed.
     */
    fun show(
        activity: FragmentActivity,
        onSetUp: () -> Unit,
        onCancel: () -> Unit,
    ): AlertDialog
}

/** Default implementation of [AutomotiveAuthenticationSetupPrompt]. */
object DefaultAutomotiveAuthenticationSetupPrompt : AutomotiveAuthenticationSetupPrompt {
    override fun show(
        activity: FragmentActivity,
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
                    activity.lifecycle.addObserver(DeviceLockSetupObserver(activity, onSetUp, onCancel))
                } catch (_: ActivityNotFoundException) {
                    onCancel()
                }
            }
            .setOnCancelListener { onCancel() }
            .show()
            .withCenterAlignedButtons()
            .apply { secure(activity) }
}

/**
 * Checks whether a device lock was set up once the user comes back to [activity] from the device security settings. The
 * activity has to be paused first, as a newly added observer receives the current resumed state immediately.
 */
private class DeviceLockSetupObserver(
    private val activity: FragmentActivity,
    private val onSetUp: () -> Unit,
    private val onCancel: () -> Unit,
) : LifecycleEventObserver {
    private var hasLeftActivity = false

    override fun onStateChanged(source: LifecycleOwner, event: Lifecycle.Event) {
        when (event) {
            Lifecycle.Event.ON_PAUSE -> hasLeftActivity = true
            Lifecycle.Event.ON_RESUME ->
                if (hasLeftActivity) {
                    source.lifecycle.removeObserver(this)
                    if (activity.isDeviceLockSetUp()) {
                        onSetUp()
                    } else {
                        onCancel()
                    }
                }
            Lifecycle.Event.ON_DESTROY -> source.lifecycle.removeObserver(this)
            else -> Unit
        }
    }
}
