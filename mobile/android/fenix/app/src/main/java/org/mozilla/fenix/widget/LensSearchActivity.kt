/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.widget

import android.content.Intent
import android.os.Bundle
import androidx.activity.result.ActivityResult
import androidx.activity.result.ActivityResultLauncher
import androidx.activity.result.contract.ActivityResultContracts
import androidx.annotation.VisibleForTesting
import androidx.appcompat.app.AppCompatActivity
import androidx.core.net.toUri
import mozilla.components.feature.intent.ext.sanitize
import mozilla.components.support.ktx.android.net.isHttpOrHttps
import mozilla.components.support.ktx.kotlin.toNormalizedUrl
import org.mozilla.fenix.GleanMetrics.SearchWidget
import org.mozilla.fenix.HomeActivity
import org.mozilla.fenix.IntentReceiverActivity
import org.mozilla.fenix.components.lens.CameraMode
import org.mozilla.fenix.components.lens.LensCameraActivity
import org.mozilla.fenix.components.showQrScanConfirmationDialog
import org.mozilla.fenix.components.showQrScanInvalidUrlDialog

/**
 * Launched directly by the search widget's Google Lens button so the Lens camera opens without first showing the home
 * screen. It starts [LensCameraActivity], which uploads the image, then opens the Lens result in the browser via
 * [HomeActivity.OPEN_TO_BROWSER_AND_LOAD]. A scanned QR code is forwarded to the browser as well.
 */
class LensSearchActivity : AppCompatActivity() {

    private lateinit var cameraLauncher: ActivityResultLauncher<Intent>

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        cameraLauncher =
            registerForActivityResult(ActivityResultContracts.StartActivityForResult()) {
                handleCameraResult(it)
            }

        // Only launch the camera on the initial creation; on recreation the in-flight result is
        // still delivered to the re-registered launcher above.
        if (savedInstanceState == null) {
            SearchWidget.lensButton.record(
                SearchWidget.LensButtonExtra(widgetType = intent?.getStringExtra(SearchWidgetIntents.EXTRA_WIDGET_TYPE))
            )
            cameraLauncher.launch(LensCameraActivity.newIntent(this, isPrivate = false, initialMode = CameraMode.LENS))
        }
    }

    @VisibleForTesting
    internal fun handleCameraResult(result: ActivityResult) {
        if (result.resultCode != RESULT_OK) {
            finish()
            return
        }

        val data = result.data
        val qrString = data?.getStringExtra(LensCameraActivity.EXTRA_SCAN_RESULT_DATA)
        if (!qrString.isNullOrEmpty()) {
            handleQrScanResult(qrString)
            return
        }

        data?.getStringExtra(LensCameraActivity.EXTRA_LENS_RESULT_URL)?.let { forwardToBrowser(it) }
        finish()
    }

    /**
     * QR content is attacker controlled physical input, so the scan goes through the same confirmation gate as the
     * toolbar QR scanner instead of navigating on its own.
     */
    private fun handleQrScanResult(qrString: String) {
        val normalizedUrl = qrString.toNormalizedUrl()
        if (!normalizedUrl.toUri().isHttpOrHttps) {
            showQrScanInvalidUrlDialog(onDismiss = ::finish)
        } else {
            showQrScanConfirmationDialog(
                url = normalizedUrl,
                onConfirm = {
                    forwardToBrowser(normalizedUrl)
                    finish()
                },
                onDeny = ::finish,
            )
        }
    }

    private fun forwardToBrowser(urlOrSearchTerm: String) {
        val intent =
            Intent(this, IntentReceiverActivity::class.java).apply {
                putExtra(HomeActivity.OPEN_TO_BROWSER_AND_LOAD, true)
                putExtra(HomeActivity.LENS_RESULT_URL, urlOrSearchTerm)
            }
        startActivity(intent.sanitize())
    }
}
