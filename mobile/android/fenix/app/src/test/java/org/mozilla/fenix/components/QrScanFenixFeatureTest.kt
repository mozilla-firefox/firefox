/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.components

import android.content.Intent
import androidx.activity.result.ActivityResultLauncher
import io.mockk.every
import io.mockk.mockk
import io.mockk.slot
import kotlinx.coroutines.test.StandardTestDispatcher
import kotlinx.coroutines.test.runTest
import mozilla.components.feature.qr.QrScanActivity
import mozilla.components.support.test.robolectric.testContext
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import org.mozilla.fenix.components.appstate.AppAction.QrScannerAction
import org.mozilla.fenix.components.lens.LensCameraActivity
import org.robolectric.RobolectricTestRunner

@RunWith(RobolectricTestRunner::class)
class QrScanFenixFeatureTest {

    private val testDispatcher = StandardTestDispatcher()
    private val appStore = AppStore()
    private val qrScanLauncher: ActivityResultLauncher<Intent> = mockk(relaxed = true)
    private val intentSlot = slot<Intent>()
    private var feature: QrScanFenixFeature? = null

    @After
    fun tearDown() {
        feature?.stop()
    }

    private fun startFeature(isLensCameraEnabled: Boolean) {
        every { qrScanLauncher.launch(capture(intentSlot)) } returns Unit
        feature =
            QrScanFenixFeature(
                    context = testContext,
                    appStore = appStore,
                    qrScanActivityLauncher = qrScanLauncher,
                    mainDispatcher = testDispatcher,
                    isLensCameraEnabled = { isLensCameraEnabled },
                )
                .also { it.start() }
    }

    @Test
    fun `GIVEN the Lens camera is enabled WHEN QrScannerRequested is dispatched THEN LensCameraActivity is launched in QR only mode`() =
        runTest(testDispatcher) {
            startFeature(isLensCameraEnabled = true)

            appStore.dispatch(QrScannerAction.QrScannerRequested)
            testDispatcher.scheduler.advanceUntilIdle()

            assertEquals(LensCameraActivity::class.java.name, intentSlot.captured.component?.className)
            assertTrue(intentSlot.captured.getBooleanExtra(LensCameraActivity.EXTRA_QR_ONLY, false))
        }

    @Test
    fun `GIVEN the Lens camera is disabled WHEN QrScannerRequested is dispatched THEN QrScanActivity is launched`() =
        runTest(testDispatcher) {
            startFeature(isLensCameraEnabled = false)

            appStore.dispatch(QrScannerAction.QrScannerRequested)
            testDispatcher.scheduler.advanceUntilIdle()

            assertEquals(QrScanActivity::class.java.name, intentSlot.captured.component?.className)
        }
}
