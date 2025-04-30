/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.settings

import android.content.pm.PackageManager
import android.os.Build
import android.os.Bundle
import android.os.VibrationEffect
import android.os.Vibrator
import android.view.View
import androidx.core.content.ContextCompat
import androidx.core.content.getSystemService
import androidx.fragment.app.Fragment
import androidx.navigation.fragment.findNavController
import androidx.navigation.fragment.navArgs
import mozilla.components.feature.qr.QrFeature
import mozilla.components.service.fxa.manager.SCOPE_PROFILE
import mozilla.components.service.fxa.manager.SCOPE_SESSION
import mozilla.components.service.fxa.manager.SCOPE_SYNC
import mozilla.components.support.base.feature.UserInteractionHandler
import mozilla.components.support.base.feature.ViewBoundFeatureWrapper
import org.mozilla.fenix.R
import org.mozilla.fenix.ext.requireComponents
import org.mozilla.fenix.ext.settings
import org.mozilla.fenix.ext.showToolbar

class PairFragment : Fragment(R.layout.fragment_pair), UserInteractionHandler {
    private val args by navArgs<PairFragmentArgs>()

    private val qrFeature = ViewBoundFeatureWrapper<QrFeature>()

    @Suppress("DEPRECATION")
    // https://github.com/mozilla-mobile/fenix/issues/19920
    override fun onViewCreated(view: View, savedInstanceState: Bundle?) {
        super.onViewCreated(view, savedInstanceState)

        qrFeature.set(
            QrFeature(
                requireContext(),
                fragmentManager = parentFragmentManager,
                onNeedToRequestPermissions = { permissions ->
                    requestPermissions(permissions, REQUEST_CODE_CAMERA_PERMISSIONS)
                },
                onScanResult = { pairingUrl ->
                    // By the time we get a scan result, we may not be attached to the context anymore.
                    // See https://github.com/mozilla-mobile/fenix/issues/15812
                    if (context == null) {
                        findNavController().popBackStack(
                            R.id.turnOnSyncFragment,
                            false,
                        )
                        return@QrFeature
                    }
                    requireComponents.services.accountsAuthFeature.beginPairingAuthentication(
                        requireContext(),
                        pairingUrl,
                        args.entrypoint,
                        setOf(SCOPE_SYNC, SCOPE_PROFILE, SCOPE_SESSION),
                    )
                    val vibrator = requireContext().getSystemService<Vibrator>()!!
                    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
                        vibrator.vibrate(
                            VibrationEffect.createOneShot(
                                VIBRATE_LENGTH,
                                VibrationEffect.DEFAULT_AMPLITUDE,
                            ),
                        )
                    } else {
                        @Suppress("Deprecation")
                        vibrator.vibrate(VIBRATE_LENGTH)
                    }
                    findNavController().popBackStack(
                        R.id.turnOnSyncFragment,
                        false,
                    )
                },
                scanMessage = R.string.pair_instructions_2,
            ),
            owner = this,
            view = view,
        )

        qrFeature.withFeature {
            it.scan(R.id.pair_layout)
        }
    }

    override fun onResume() {
        super.onResume()
        showToolbar(getString(R.string.sync_scan_code))
    }

    override fun onBackPressed(): Boolean {
        qrFeature.onBackPressed()
        findNavController().popBackStack(R.id.turnOnSyncFragment, false)
        return true
    }

    companion object {
        private const val REQUEST_CODE_CAMERA_PERMISSIONS = 1
        private const val VIBRATE_LENGTH = 200L
    }

    @Suppress("OVERRIDE_DEPRECATION")
    override fun onRequestPermissionsResult(
        requestCode: Int,
        permissions: Array<String>,
        grantResults: IntArray,
    ) {
        when (requestCode) {
            REQUEST_CODE_CAMERA_PERMISSIONS -> {
                if (ContextCompat.checkSelfPermission(
                        requireContext(),
                        android.Manifest.permission.CAMERA,
                    ) == PackageManager.PERMISSION_GRANTED
                ) {
                    qrFeature.withFeature {
                        it.onPermissionsResult(permissions, grantResults)
                    }
                } else {
                    findNavController().popBackStack(R.id.turnOnSyncFragment, false)
                }
            }
        }
    }
}
