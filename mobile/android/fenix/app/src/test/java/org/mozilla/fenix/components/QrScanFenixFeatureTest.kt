/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.components

import android.app.Activity
import android.app.Application
import android.content.Context
import android.content.Intent
import androidx.activity.ComponentActivity
import androidx.activity.result.ActivityResultLauncher
import io.mockk.every
import io.mockk.mockk
import io.mockk.slot
import kotlin.test.assertNotNull
import kotlinx.coroutines.test.StandardTestDispatcher
import kotlinx.coroutines.test.runTest
import mozilla.components.feature.qr.QrScanActivity
import mozilla.components.support.test.robolectric.testContext
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith
import org.mozilla.fenix.GleanMetrics.SyncAuth
import org.mozilla.fenix.components.appstate.AppAction.QrScannerAction
import org.mozilla.fenix.components.lens.LensCameraActivity
import org.mozilla.fenix.helpers.FenixGleanTestRule
import org.mozilla.fenix.settings.account.AuthIntentReceiverActivity
import org.robolectric.Robolectric
import org.robolectric.RobolectricTestRunner
import org.robolectric.Shadows.shadowOf
import org.robolectric.shadows.ShadowDialog

@RunWith(RobolectricTestRunner::class)
class QrScanFenixFeatureTest {

    @get:Rule val gleanTestRule = FenixGleanTestRule(testContext)

    private val testDispatcher = StandardTestDispatcher()
    private val appStore = AppStore()
    private val qrScanLauncher: ActivityResultLauncher<Intent> = mockk(relaxed = true)
    private val intentSlot = slot<Intent>()
    private var feature: QrScanFenixFeature? = null

    @After
    fun tearDown() {
        feature?.stop()
    }

    private fun createFeature(
        context: Context = testContext,
        isLensCameraEnabled: Boolean = false,
        isPairingLink: Boolean = false,
    ): QrScanFenixFeature {
        every { qrScanLauncher.launch(capture(intentSlot)) } returns Unit
        return QrScanFenixFeature(
                context = context,
                appStore = appStore,
                qrScanActivityLauncher = qrScanLauncher,
                mainDispatcher = testDispatcher,
                isLensCameraEnabled = { isLensCameraEnabled },
                isPairingLink = { isPairingLink },
            )
            .also { feature = it }
    }

    private fun scanResult(url: String) = Intent().putExtra(QrScanActivity.EXTRA_SCAN_RESULT_DATA, url)

    @Test
    fun `GIVEN the Lens camera is enabled WHEN QrScannerRequested is dispatched THEN LensCameraActivity is launched in QR only mode`() =
        runTest(testDispatcher) {
            createFeature(isLensCameraEnabled = true).start()

            appStore.dispatch(QrScannerAction.QrScannerRequested)
            testDispatcher.scheduler.advanceUntilIdle()

            assertEquals(LensCameraActivity::class.java.name, intentSlot.captured.component?.className)
            assertTrue(intentSlot.captured.getBooleanExtra(LensCameraActivity.EXTRA_QR_ONLY, false))
        }

    @Test
    fun `GIVEN the Lens camera is disabled WHEN QrScannerRequested is dispatched THEN QrScanActivity is launched`() =
        runTest(testDispatcher) {
            createFeature(isLensCameraEnabled = false).start()

            appStore.dispatch(QrScannerAction.QrScannerRequested)
            testDispatcher.scheduler.advanceUntilIdle()

            assertEquals(QrScanActivity::class.java.name, intentSlot.captured.component?.className)
        }

    @Test
    fun `GIVEN a scanned pairing link WHEN handleToolbarQrScanResults is called THEN the auth custom tab is opened without the confirmation dialog`() {
        val activity = Robolectric.buildActivity(ComponentActivity::class.java).get()
        val feature = createFeature(context = activity, isPairingLink = true)
        feature.handleToolbarQrScanResults(Activity.RESULT_OK, scanResult(PAIRING_URL))

        val startedIntent = shadowOf(activity).peekNextStartedActivity()
        assertEquals(AuthIntentReceiverActivity::class.java.name, startedIntent.component?.className)
        assertEquals(PAIRING_URL_IN_CUSTOM_TAB, startedIntent.dataString)
        assertNull(ShadowDialog.getLatestDialog())
        assertNull(appStore.state.qrScannerState.lastScanData)
    }

    @Test
    fun `GIVEN a scanned pairing link WHEN handleToolbarQrScanResults is called THEN pairingLinkOpened is recorded`() {
        val activity = Robolectric.buildActivity(ComponentActivity::class.java).get()
        val feature = createFeature(context = activity, isPairingLink = true)
        assertNull(SyncAuth.pairingLinkOpened.testGetValue())

        feature.handleToolbarQrScanResults(Activity.RESULT_OK, scanResult(PAIRING_URL))

        assertNotNull(SyncAuth.pairingLinkOpened.testGetValue())
    }

    @Test
    fun `GIVEN a scanned non pairing link WHEN handleToolbarQrScanResults is called THEN the confirmation dialog is shown and no activity starts`() {
        val feature = createFeature(isPairingLink = false)
        feature.handleToolbarQrScanResults(Activity.RESULT_OK, scanResult("https://example.com"))

        assertNull(shadowOf(testContext as Application).peekNextStartedActivity())
        assertNotNull(ShadowDialog.getLatestDialog())
    }

    private companion object {
        const val PAIRING_URL = "https://accounts.firefox.com/pair#channel_id=test_id&channel_key=test_key&v=2"
        const val PAIRING_URL_IN_CUSTOM_TAB =
            "https://accounts.firefox.com/pair?embed=customtab#channel_id=test_id&channel_key=test_key&v=2"
    }
}
