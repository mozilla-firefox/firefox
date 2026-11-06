/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.components.lens

import android.Manifest
import android.app.Activity
import android.app.Application
import android.content.Intent
import android.graphics.Bitmap
import android.net.Uri
import android.os.Bundle
import android.os.Looper
import android.view.ViewGroup
import androidx.compose.ui.platform.ComposeView
import androidx.core.view.isVisible
import androidx.test.core.app.ApplicationProvider
import com.google.android.material.bottomsheet.BottomSheetBehavior
import com.google.android.material.bottomsheet.BottomSheetDialog
import io.mockk.coEvery
import io.mockk.coVerify
import io.mockk.every
import io.mockk.mockk
import io.mockk.verify
import java.io.ByteArrayInputStream
import java.io.File
import java.time.Duration
import kotlin.test.assertIs
import kotlin.test.assertNotNull
import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.test.UnconfinedTestDispatcher
import mozilla.components.feature.qr.QrScanActivity
import mozilla.components.support.test.robolectric.testContext
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertSame
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith
import org.mozilla.fenix.BuildConfig
import org.mozilla.fenix.GleanMetrics.GoogleLens
import org.mozilla.fenix.R
import org.mozilla.fenix.components.AppStore
import org.mozilla.fenix.components.LensImageUploader
import org.mozilla.fenix.ext.components
import org.mozilla.fenix.helpers.FenixGleanTestRule
import org.mozilla.fenix.helpers.SHADOW_HEIGHT
import org.mozilla.fenix.helpers.SHADOW_WIDTH
import org.mozilla.fenix.helpers.ShadowBoundsReportingBitmapFactory
import org.mozilla.fenix.utils.Settings
import org.robolectric.Robolectric
import org.robolectric.RobolectricTestRunner
import org.robolectric.Shadows
import org.robolectric.android.controller.ActivityController
import org.robolectric.annotation.Config
import org.robolectric.shadows.ShadowToast

@RunWith(RobolectricTestRunner::class)
class LensCameraActivityTest {

    @get:Rule val gleanTestRule = FenixGleanTestRule(testContext)

    private lateinit var settings: Settings
    private val uploader: LensImageUploader = mockk()
    private val pickedImageUri = Uri.parse("content://test/picked_image.jpg")
    private val capturedImageUri = Uri.parse("content://test/lens_capture.jpg")
    private val pickedImage = Bitmap.createBitmap(80, 40, Bitmap.Config.ARGB_8888)

    @Before
    fun setUp() {
        settings = testContext.components.settings
        // Most tests exercise the camera flow, which is only reached once the opt-out sheet has
        // been acknowledged; the opt-out tests below override this.
        every { settings.hasAcceptedGoogleLensFirstRun } returns true
        every { settings.lensCameraLastMode } returns CameraMode.LENS
        // FirefoxTheme, used by the opt-out sheet, resolves the theme through FenixApplication,
        // which reads the AppStore's browsing mode.
        every { testContext.components.appStore } returns AppStore()
        // Read by Snackbar.make, which the upload failure messages use.
        every { settings.accessibilityServicesEnabled } returns false
    }

    @Test
    fun `GIVEN a context WHEN newIntent is called THEN the returned intent targets LensCameraActivity`() {
        val intent = LensCameraActivity.newIntent(testContext, isPrivate = false)
        assertEquals(
            LensCameraActivity::class.java.name,
            intent.component?.className,
        )
    }

    @Test
    fun `GIVEN an initial mode WHEN newIntent is called THEN the mode is carried as an extra`() {
        val intent = LensCameraActivity.newIntent(testContext, isPrivate = false, initialMode = CameraMode.QR)

        assertEquals("QR", intent.getStringExtra(LensCameraActivity.EXTRA_INITIAL_MODE))
    }

    @Test
    fun `GIVEN no initial mode WHEN newIntent is called THEN no mode extra is set`() {
        val intent = LensCameraActivity.newIntent(testContext, isPrivate = false)

        assertFalse(intent.hasExtra(LensCameraActivity.EXTRA_INITIAL_MODE))
    }

    @Test
    fun `GIVEN an initial mode extra WHEN resolveInitialMode is called THEN the extra wins over the last used mode`() {
        every { settings.lensCameraLastMode } returns CameraMode.QR
        val intent = LensCameraActivity.newIntent(testContext, isPrivate = false, initialMode = CameraMode.LENS)
        val activity = Robolectric.buildActivity(LensCameraActivity::class.java, intent).create().get()

        assertEquals(CameraMode.LENS, activity.resolveInitialMode())
    }

    @Test
    fun `GIVEN no initial mode extra WHEN resolveInitialMode is called THEN the last used mode is returned`() {
        every { settings.lensCameraLastMode } returns CameraMode.QR
        val activity = Robolectric.buildActivity(LensCameraActivity::class.java).create().get()

        assertEquals(CameraMode.QR, activity.resolveInitialMode())
    }

    @Test
    fun `GIVEN an unknown initial mode extra WHEN resolveInitialMode is called THEN the last used mode is returned`() {
        every { settings.lensCameraLastMode } returns CameraMode.QR
        val intent =
            LensCameraActivity.newIntent(testContext, isPrivate = false)
                .putExtra(LensCameraActivity.EXTRA_INITIAL_MODE, "UNKNOWN")
        val activity = Robolectric.buildActivity(LensCameraActivity::class.java, intent).create().get()

        assertEquals(CameraMode.QR, activity.resolveInitialMode())
    }

    @Test
    fun `GIVEN a context WHEN newQrScannerIntent is called THEN the intent targets LensCameraActivity in QR only mode`() {
        val intent = LensCameraActivity.newQrScannerIntent(testContext)

        assertEquals(LensCameraActivity::class.java.name, intent.component?.className)
        assertTrue(intent.getBooleanExtra(LensCameraActivity.EXTRA_QR_ONLY, false))
        assertFalse(intent.hasExtra(LensCameraActivity.EXTRA_IS_PRIVATE))
        assertFalse(intent.hasExtra(LensCameraActivity.EXTRA_INITIAL_MODE))
    }

    @Test
    fun `GIVEN a context WHEN newIntent is called THEN the intent is not QR only`() {
        val intent = LensCameraActivity.newIntent(testContext, isPrivate = false, initialMode = CameraMode.QR)

        assertFalse(intent.hasExtra(LensCameraActivity.EXTRA_QR_ONLY))
    }

    @Test
    fun `GIVEN a QR scanner intent with a LENS initial mode extra WHEN resolveInitialMode is called THEN QR is returned`() {
        val intent =
            LensCameraActivity.newQrScannerIntent(testContext)
                .putExtra(LensCameraActivity.EXTRA_INITIAL_MODE, CameraMode.LENS.name)
        val activity = Robolectric.buildActivity(LensCameraActivity::class.java, intent).create().get()

        assertEquals(CameraMode.QR, activity.resolveInitialMode())
    }

    @Test
    fun `GIVEN QR only WHEN the camera fragment is added THEN it is told to open QR only in QR mode`() {
        val intent = LensCameraActivity.newQrScannerIntent(testContext)
        val activity = Robolectric.buildActivity(LensCameraActivity::class.java, intent).create().get()

        activity.handlePermissionResult(isGranted = true)
        activity.supportFragmentManager.executePendingTransactions()

        val fragment = activity.supportFragmentManager.findFragmentById(R.id.lens_fragment_container_view)
        assertTrue(fragment?.arguments?.getBoolean(LensCameraFragment.ARG_QR_ONLY) == true)
        assertEquals("QR", fragment?.arguments?.getString(LensCameraFragment.ARG_INITIAL_MODE))
    }

    @Test
    fun `GIVEN the last used mode is QR WHEN the camera fragment is added THEN it is told to open in QR mode`() {
        every { settings.lensCameraLastMode } returns CameraMode.QR
        val activity = Robolectric.buildActivity(LensCameraActivity::class.java).create().get()

        activity.handlePermissionResult(isGranted = true)
        activity.supportFragmentManager.executePendingTransactions()

        val fragment = activity.supportFragmentManager.findFragmentById(R.id.lens_fragment_container_view)
        assertEquals("QR", fragment?.arguments?.getString(LensCameraFragment.ARG_INITIAL_MODE))
    }

    @Test
    fun `WHEN the camera fragment reports a mode change THEN it is saved as the last used mode and the activity stays open`() {
        val activity = Robolectric.buildActivity(LensCameraActivity::class.java).setup().get()

        activity.supportFragmentManager.setFragmentResult(
            LensCameraFragment.MODE_CHANGED_REQUEST_KEY,
            Bundle().apply { putString(LensCameraFragment.RESULT_CAMERA_MODE, "QR") },
        )
        activity.supportFragmentManager.executePendingTransactions()

        verify { settings.lensCameraLastMode = CameraMode.QR }
        assertFalse(activity.isFinishing)
    }

    @Test
    fun `GIVEN cached lens images exist WHEN clearLensImageCache is called THEN cached files are deleted and the directory remains`() {
        val activity = Robolectric.buildActivity(LensCameraActivity::class.java).get()
        val imageDir = File(activity.cacheDir, "lens_images")
        imageDir.mkdirs()
        val testFile = File(imageDir, "test_image.jpg")
        testFile.writeText("test")
        assertTrue(testFile.exists())

        activity.clearLensImageCache()

        assertFalse(testFile.exists())
        assertTrue(imageDir.exists())
    }

    @Test
    fun `GIVEN the lens image cache directory does not exist WHEN clearLensImageCache is called THEN no exception is thrown`() {
        val activity = Robolectric.buildActivity(LensCameraActivity::class.java).get()
        val imageDir = File(activity.cacheDir, "lens_images")
        assertFalse(imageDir.exists())

        activity.clearLensImageCache()
    }

    @Test
    fun `WHEN handlePermissionResult is called with false THEN a permission-denied Toast is shown, RESULT_CANCELED is set, and the activity finishes`() {
        val activity = Robolectric.buildActivity(LensCameraActivity::class.java).create().get()

        activity.handlePermissionResult(isGranted = false)

        assertEquals("Permission denied", ShadowToast.getTextOfLatestToast())
        assertEquals(Activity.RESULT_CANCELED, Shadows.shadowOf(activity).resultCode)
        assertTrue(activity.isFinishing)
    }

    @Test
    fun `WHEN handlePermissionResult is called with true THEN a LensCameraFragment is added to the container`() {
        val activity = Robolectric.buildActivity(LensCameraActivity::class.java).create().get()

        activity.handlePermissionResult(isGranted = true)
        activity.supportFragmentManager.executePendingTransactions()

        val fragment = activity.supportFragmentManager.findFragmentById(R.id.lens_fragment_container_view)
        assertNotNull(fragment)
        assertIs<LensCameraFragment>(fragment)
    }

    @Test
    fun `GIVEN a LensCameraFragment is already in the container WHEN handlePermissionResult is called with true THEN no duplicate fragment is added`() {
        val activity = Robolectric.buildActivity(LensCameraActivity::class.java).create().get()
        activity.handlePermissionResult(isGranted = true)
        activity.supportFragmentManager.executePendingTransactions()
        val firstFragment = activity.supportFragmentManager.findFragmentById(R.id.lens_fragment_container_view)

        activity.handlePermissionResult(isGranted = true)
        activity.supportFragmentManager.executePendingTransactions()

        val fragments = activity.supportFragmentManager.fragments
        assertEquals(1, fragments.size)
        assertSame(firstFragment, fragments[0])
    }

    @Test
    fun `GIVEN camera permission is granted WHEN the activity reaches onResume THEN a LensCameraFragment is added`() {
        val shadowApp = Shadows.shadowOf(ApplicationProvider.getApplicationContext<Application>())
        shadowApp.grantPermissions(Manifest.permission.CAMERA)

        val controller = Robolectric.buildActivity(LensCameraActivity::class.java).setup()
        val activity = controller.get()
        activity.supportFragmentManager.executePendingTransactions()

        val fragment = activity.supportFragmentManager.findFragmentById(R.id.lens_fragment_container_view)
        assertNotNull(fragment)
        assertIs<LensCameraFragment>(fragment)
    }

    @Test
    fun `WHEN the fragment result bundle carries RESULT_QR_STRING THEN the activity returns RESULT_OK with EXTRA_SCAN_RESULT_DATA and finishes`() {
        val controller = Robolectric.buildActivity(LensCameraActivity::class.java).setup()
        val activity = controller.get()
        val qrString = "https://example.com"

        activity.supportFragmentManager.setFragmentResult(
            LensCameraFragment.RESULT_REQUEST_KEY,
            Bundle().apply { putString(LensCameraFragment.RESULT_QR_STRING, qrString) },
        )
        activity.supportFragmentManager.executePendingTransactions()

        val shadow = Shadows.shadowOf(activity)
        assertEquals(Activity.RESULT_OK, shadow.resultCode)
        assertEquals(qrString, shadow.resultIntent.getStringExtra(QrScanActivity.EXTRA_SCAN_RESULT_DATA))
        assertTrue(activity.isFinishing)
    }

    @Test
    fun `GIVEN both QR string and image URI are present WHEN the fragment result fires THEN the QR string takes priority`() {
        val controller = Robolectric.buildActivity(LensCameraActivity::class.java).setup()
        val activity = controller.get()
        val qrString = "https://example.com"

        activity.supportFragmentManager.setFragmentResult(
            LensCameraFragment.RESULT_REQUEST_KEY,
            Bundle().apply {
                putString(LensCameraFragment.RESULT_QR_STRING, qrString)
                putBoolean(LensCameraFragment.RESULT_GALLERY_REQUEST, true)
            },
        )
        activity.supportFragmentManager.executePendingTransactions()

        val shadow = Shadows.shadowOf(activity)
        assertEquals(Activity.RESULT_OK, shadow.resultCode)
        assertEquals(qrString, shadow.resultIntent.getStringExtra(QrScanActivity.EXTRA_SCAN_RESULT_DATA))
        assertTrue(activity.isFinishing)
    }

    @Test
    fun `GIVEN a decoded QR string WHEN handleQrDecodeResult is called THEN the activity finishes with EXTRA_SCAN_RESULT_DATA`() {
        val controller = Robolectric.buildActivity(LensCameraActivity::class.java).setup()
        val activity = controller.get()
        val qrString = "https://example.com/from-gallery"

        activity.handleQrDecodeResult(qrString)

        val shadow = Shadows.shadowOf(activity)
        assertEquals(Activity.RESULT_OK, shadow.resultCode)
        assertEquals(qrString, shadow.resultIntent.getStringExtra(QrScanActivity.EXTRA_SCAN_RESULT_DATA))
        assertTrue(activity.isFinishing)
    }

    @Test
    fun `GIVEN no QR decoded from the picked image WHEN handleQrDecodeResult is called with null THEN a toast is shown and the activity stays open`() {
        val controller = Robolectric.buildActivity(LensCameraActivity::class.java).setup()
        val activity = controller.get()

        activity.handleQrDecodeResult(null)

        assertEquals(
            activity.getString(R.string.lens_camera_qr_no_code_found),
            ShadowToast.getTextOfLatestToast(),
        )
        assertFalse(activity.isFinishing)
    }

    @Test
    fun `GIVEN an empty QR result WHEN handleQrDecodeResult is called THEN a toast is shown and the activity stays open`() {
        val controller = Robolectric.buildActivity(LensCameraActivity::class.java).setup()
        val activity = controller.get()

        activity.handleQrDecodeResult("")

        assertEquals(
            activity.getString(R.string.lens_camera_qr_no_code_found),
            ShadowToast.getTextOfLatestToast(),
        )
        assertFalse(activity.isFinishing)
    }

    @Test
    fun `WHEN the fragment result bundle has an empty QR string THEN the QR branch is ignored`() {
        val controller = Robolectric.buildActivity(LensCameraActivity::class.java).setup()
        val activity = controller.get()

        activity.supportFragmentManager.setFragmentResult(
            LensCameraFragment.RESULT_REQUEST_KEY,
            Bundle().apply { putString(LensCameraFragment.RESULT_QR_STRING, "") },
        )
        activity.supportFragmentManager.executePendingTransactions()

        // No QR result means we fall through; empty bundle (no image URI, no gallery flag)
        // sets RESULT_CANCELED and finishes.
        val shadow = Shadows.shadowOf(activity)
        assertEquals(Activity.RESULT_CANCELED, shadow.resultCode)
        assertTrue(activity.isFinishing)
    }

    @Test
    fun `WHEN the camera returns a captured image THEN the camera gives way to the upload screen and the activity stays open`() {
        coEvery { uploader.decodeImage(capturedImageUri) } returns pickedImage
        coEvery { uploader.uploadImage(any(), any()) } coAnswers
            {
                CompletableDeferred<LensImageUploader.UploadResult>().await()
            }
        val activity = cameraActivityController().get()

        returnCapturedImage(activity)

        assertNull(activity.supportFragmentManager.findFragmentById(R.id.lens_fragment_container_view))
        assertTrue(activity.findViewById<ComposeView>(R.id.lens_upload_overlay).isVisible)
        assertFalse(activity.isFinishing)
    }

    @Test
    fun `GIVEN a captured image WHEN the upload succeeds THEN the activity returns the Lens result URL and records a camera search`() {
        coEvery { uploader.decodeImage(capturedImageUri) } returns pickedImage
        coEvery { uploader.uploadImage(any(), any()) } returns
            LensImageUploader.UploadResult(resultUrl = LENS_RESULT_URL, httpStatusCode = 200)
        val activity = cameraActivityController(isPrivate = true).get()

        returnCapturedImage(activity)
        Shadows.shadowOf(Looper.getMainLooper()).idle()

        val shadow = Shadows.shadowOf(activity)
        assertEquals(Activity.RESULT_OK, shadow.resultCode)
        assertEquals(LENS_RESULT_URL, shadow.resultIntent.getStringExtra(LensCameraActivity.EXTRA_LENS_RESULT_URL))
        assertTrue(shadow.resultIntent.getBooleanExtra(LensCameraActivity.EXTRA_IS_PRIVATE, false))
        assertNull(shadow.resultIntent.data)
        assertTrue(activity.isFinishing)
        coVerify { uploader.uploadImage(pickedImage, isPrivate = true) }
        val events = assertNotNull(GoogleLens.searchCompleted.testGetValue())
        assertEquals(LensCameraActivity.IMAGE_SOURCE_CAMERA, events.single().extra?.get("source"))
    }

    @Test
    fun `GIVEN a captured image WHEN the upload fails and Try again is tapped THEN the camera comes back and the capture is uploaded again as a camera search`() {
        coEvery { uploader.decodeImage(capturedImageUri) } returns pickedImage
        coEvery { uploader.uploadImage(any(), any()) } returns
            LensImageUploader.UploadResult(resultUrl = null, httpStatusCode = 500) andThen
            LensImageUploader.UploadResult(resultUrl = LENS_RESULT_URL, httpStatusCode = 200)
        val activity = cameraActivityController().get()
        returnCapturedImage(activity)
        Shadows.shadowOf(Looper.getMainLooper()).idle()

        assertIs<LensCameraFragment>(
            activity.supportFragmentManager.findFragmentById(R.id.lens_fragment_container_view)
        )
        assertFalse(activity.isFinishing)

        assertNotNull(activity.uploadOverlay.failureSnackbarState?.action).onClick()
        Shadows.shadowOf(Looper.getMainLooper()).idle()

        assertTrue(activity.isFinishing)
        coVerify(exactly = 2) { uploader.decodeImage(capturedImageUri) }
        val events = assertNotNull(GoogleLens.searchCompleted.testGetValue())
        assertEquals(
            listOf(LensCameraActivity.IMAGE_SOURCE_CAMERA, LensCameraActivity.IMAGE_SOURCE_CAMERA),
            events.map { it.extra?.get("source") },
        )
    }

    @Test
    fun `WHEN a photo picker image is returned THEN the camera gives way to the upload screen and the activity stays open`() {
        uploadNeverFinishes()
        val activity = cameraActivityController().get()

        activity.handleGalleryResult(pickedImageUri)

        assertNull(activity.supportFragmentManager.findFragmentById(R.id.lens_fragment_container_view))
        assertTrue(activity.findViewById<ComposeView>(R.id.lens_upload_overlay).isVisible)
        assertFalse(activity.isFinishing)
    }

    @Test
    fun `GIVEN a photo picker image WHEN the upload succeeds THEN the activity returns the Lens result URL and finishes`() {
        coEvery { uploader.decodeImage(pickedImageUri) } returns pickedImage
        coEvery { uploader.uploadImage(any(), any()) } returns
            LensImageUploader.UploadResult(resultUrl = LENS_RESULT_URL, httpStatusCode = 200)
        val activity = cameraActivityController(isPrivate = true).get()

        activity.handleGalleryResult(pickedImageUri)
        Shadows.shadowOf(Looper.getMainLooper()).idle()

        val shadow = Shadows.shadowOf(activity)
        assertEquals(Activity.RESULT_OK, shadow.resultCode)
        assertEquals(LENS_RESULT_URL, shadow.resultIntent.getStringExtra(LensCameraActivity.EXTRA_LENS_RESULT_URL))
        assertTrue(shadow.resultIntent.getBooleanExtra(LensCameraActivity.EXTRA_IS_PRIVATE, false))
        assertNull(shadow.resultIntent.data)
        assertTrue(activity.isFinishing)
        coVerify { uploader.uploadImage(pickedImage, isPrivate = true) }
    }

    @Test
    fun `GIVEN a photo picker image WHEN the upload fails THEN the camera comes back with an upload failure snackbar offering to try again`() {
        coEvery { uploader.decodeImage(pickedImageUri) } returns pickedImage
        coEvery { uploader.uploadImage(any(), any()) } returns
            LensImageUploader.UploadResult(resultUrl = null, httpStatusCode = 500)
        val activity = cameraActivityController().get()

        activity.handleGalleryResult(pickedImageUri)
        Shadows.shadowOf(Looper.getMainLooper()).idle()

        val snackbarState = assertNotNull(activity.uploadOverlay.failureSnackbarState)
        assertEquals(testContext.getString(R.string.lens_camera_upload_failed), snackbarState.message)
        assertEquals(
            testContext.getString(R.string.lens_camera_upload_failed_retry_button),
            snackbarState.action?.label,
        )
        assertTrue(activity.uploadOverlay.failureSnackbar?.isShownOrQueued == true)
        assertFalse(activity.findViewById<ComposeView>(R.id.lens_upload_overlay).isVisible)
        assertIs<LensCameraFragment>(
            activity.supportFragmentManager.findFragmentById(R.id.lens_fragment_container_view)
        )
        assertFalse(activity.isFinishing)
    }

    @Test
    fun `GIVEN a photo picker image that cannot be read WHEN it is picked THEN the camera comes back with an image load snackbar and no retry`() {
        coEvery { uploader.decodeImage(pickedImageUri) } returns null
        val activity = cameraActivityController().get()

        activity.handleGalleryResult(pickedImageUri)
        Shadows.shadowOf(Looper.getMainLooper()).idle()

        val snackbarState = assertNotNull(activity.uploadOverlay.failureSnackbarState)
        assertEquals(testContext.getString(R.string.lens_camera_image_load_failed_v2), snackbarState.message)
        assertNull(snackbarState.action)
        assertTrue(activity.uploadOverlay.failureSnackbar?.isShownOrQueued == true)
        assertFalse(activity.findViewById<ComposeView>(R.id.lens_upload_overlay).isVisible)
        assertIs<LensCameraFragment>(
            activity.supportFragmentManager.findFragmentById(R.id.lens_fragment_container_view)
        )
        assertFalse(activity.isFinishing)
        coVerify(exactly = 0) { uploader.uploadImage(any(), any()) }
    }

    @Test
    fun `GIVEN a failed upload was reported WHEN the activity is recreated THEN the failure is not reported again`() {
        coEvery { uploader.decodeImage(pickedImageUri) } returns pickedImage
        coEvery { uploader.uploadImage(any(), any()) } returns
            LensImageUploader.UploadResult(resultUrl = null, httpStatusCode = 500)
        val controller = cameraActivityController()
        controller.get().handleGalleryResult(pickedImageUri)
        Shadows.shadowOf(Looper.getMainLooper()).idle()

        val activity = controller.recreate().get()
        Shadows.shadowOf(Looper.getMainLooper()).idle()

        assertNull(activity.uploadOverlay.failureSnackbarState)
    }

    @Test
    fun `GIVEN a failed upload WHEN Try again is tapped THEN the same image is uploaded again behind the loading screen`() {
        coEvery { uploader.decodeImage(pickedImageUri) } returns pickedImage
        coEvery { uploader.uploadImage(any(), any()) } returns
            LensImageUploader.UploadResult(resultUrl = null, httpStatusCode = 500) coAndThen
            {
                CompletableDeferred<LensImageUploader.UploadResult>().await()
            }
        val activity = cameraActivityController(isPrivate = true).get()
        activity.handleGalleryResult(pickedImageUri)
        Shadows.shadowOf(Looper.getMainLooper()).idle()

        assertNotNull(activity.uploadOverlay.failureSnackbarState?.action).onClick()
        // Past the snackbar's exit animation.
        Shadows.shadowOf(Looper.getMainLooper()).idleFor(Duration.ofSeconds(1))

        assertTrue(activity.findViewById<ComposeView>(R.id.lens_upload_overlay).isVisible)
        assertNull(activity.supportFragmentManager.findFragmentById(R.id.lens_fragment_container_view))
        assertFalse(activity.uploadOverlay.failureSnackbar?.isShownOrQueued == true)
        coVerify(exactly = 2) { uploader.decodeImage(pickedImageUri) }
        coVerify(exactly = 2) { uploader.uploadImage(pickedImage, isPrivate = true) }
    }

    @Test
    fun `GIVEN a failure snackbar is showing WHEN another photo picker image is picked THEN the snackbar is dismissed`() {
        coEvery { uploader.decodeImage(pickedImageUri) } returns null
        val activity = cameraActivityController().get()
        activity.handleGalleryResult(pickedImageUri)
        Shadows.shadowOf(Looper.getMainLooper()).idle()
        uploadNeverFinishes()

        activity.handleGalleryResult(pickedImageUri)
        // Past the snackbar's exit animation.
        Shadows.shadowOf(Looper.getMainLooper()).idleFor(Duration.ofSeconds(1))

        assertFalse(activity.uploadOverlay.failureSnackbar?.isShownOrQueued == true)
        assertTrue(activity.findViewById<ComposeView>(R.id.lens_upload_overlay).isVisible)
    }

    @Test
    fun `GIVEN an upload is running WHEN back is pressed THEN the upload is cancelled instead of closing the activity`() {
        uploadNeverFinishes()
        val activity = cameraActivityController().get()
        activity.handleGalleryResult(pickedImageUri)
        Shadows.shadowOf(Looper.getMainLooper()).idle()

        activity.onBackPressedDispatcher.onBackPressed()
        activity.supportFragmentManager.executePendingTransactions()

        assertFalse(activity.findViewById<ComposeView>(R.id.lens_upload_overlay).isVisible)
        assertIs<LensCameraFragment>(
            activity.supportFragmentManager.findFragmentById(R.id.lens_fragment_container_view)
        )
        assertFalse(activity.isFinishing)
    }

    @Test
    fun `GIVEN an upload is running WHEN the activity is recreated THEN the upload screen stays up without the camera`() {
        uploadNeverFinishes()
        val controller = cameraActivityController()
        controller.get().handleGalleryResult(pickedImageUri)

        val activity = controller.recreate().get()
        activity.supportFragmentManager.executePendingTransactions()

        assertTrue(activity.findViewById<ComposeView>(R.id.lens_upload_overlay).isVisible)
        assertNull(activity.supportFragmentManager.findFragmentById(R.id.lens_fragment_container_view))
    }

    @Test
    fun `GIVEN an upload is running WHEN the activity resumes THEN the camera is not restarted underneath`() {
        uploadNeverFinishes()
        val controller = cameraActivityController()
        val activity = controller.get()
        activity.handleGalleryResult(pickedImageUri)

        controller.pause().resume()
        activity.supportFragmentManager.executePendingTransactions()

        assertNull(activity.supportFragmentManager.findFragmentById(R.id.lens_fragment_container_view))
    }

    @Test
    fun `GIVEN the permission was requested here and an upload is running WHEN the activity is restored without the upload THEN the camera comes back`() {
        uploadNeverFinishes()
        val controller = cameraActivityController()
        controller.get().permissionRequested = true
        controller.get().handleGalleryResult(pickedImageUri)
        val savedState = Bundle()
        controller.saveInstanceState(savedState)

        // A new activity instance gets a new view model, as after process death.
        val activity = cameraActivityController(savedState = savedState).get()

        assertIs<LensCameraFragment>(
            activity.supportFragmentManager.findFragmentById(R.id.lens_fragment_container_view)
        )
        assertFalse(activity.findViewById<ComposeView>(R.id.lens_upload_overlay).isVisible)
    }

    @Test
    fun `GIVEN QR only WHEN a photo picker image is returned THEN it is not uploaded`() {
        uploadNeverFinishes()
        val activity = cameraActivityController(qrOnly = true).get()

        activity.handleGalleryResult(pickedImageUri)

        assertIs<LensCameraFragment>(
            activity.supportFragmentManager.findFragmentById(R.id.lens_fragment_container_view)
        )
        assertFalse(activity.findViewById<ComposeView>(R.id.lens_upload_overlay).isVisible)
    }

    @Test
    fun `GIVEN QR only WHEN the camera fragment returns a captured image THEN it is not uploaded and the activity finishes canceled`() {
        val activity = cameraActivityController(qrOnly = true).get()

        returnCapturedImage(activity)

        assertFalse(activity.findViewById<ComposeView>(R.id.lens_upload_overlay).isVisible)
        assertEquals(Activity.RESULT_CANCELED, Shadows.shadowOf(activity).resultCode)
        assertTrue(activity.isFinishing)
    }

    @Test
    fun `GIVEN QR only WHEN the camera fragment requests the Lens gallery THEN no picker is launched and the activity finishes canceled`() {
        val activity = cameraActivityController(qrOnly = true).get()

        activity.supportFragmentManager.setFragmentResult(
            LensCameraFragment.RESULT_REQUEST_KEY,
            Bundle().apply { putBoolean(LensCameraFragment.RESULT_GALLERY_REQUEST, true) },
        )
        activity.supportFragmentManager.executePendingTransactions()

        assertNull(Shadows.shadowOf(activity).nextStartedActivityForResult)
        assertEquals(Activity.RESULT_CANCELED, Shadows.shadowOf(activity).resultCode)
        assertTrue(activity.isFinishing)
    }

    private fun returnCapturedImage(activity: LensCameraActivity) {
        activity.supportFragmentManager.setFragmentResult(
            LensCameraFragment.RESULT_REQUEST_KEY,
            Bundle().apply { putParcelable(LensCameraFragment.RESULT_IMAGE_URI, capturedImageUri) },
        )
        activity.supportFragmentManager.executePendingTransactions()
    }

    private fun uploadNeverFinishes() {
        coEvery { uploader.decodeImage(pickedImageUri) } returns pickedImage
        coEvery { uploader.uploadImage(any(), any()) } coAnswers
            {
                CompletableDeferred<LensImageUploader.UploadResult>().await()
            }
    }

    /**
     * Builds a resumed activity, restored from [savedState] if given, whose photo picker uploads go through [uploader]
     * synchronously.
     */
    @OptIn(ExperimentalCoroutinesApi::class)
    private fun cameraActivityController(
        isPrivate: Boolean = false,
        savedState: Bundle? = null,
        qrOnly: Boolean = false,
    ): ActivityController<LensCameraActivity> {
        Shadows.shadowOf(ApplicationProvider.getApplicationContext<Application>())
            .grantPermissions(Manifest.permission.CAMERA)
        val intent =
            if (qrOnly) {
                LensCameraActivity.newQrScannerIntent(testContext)
            } else {
                LensCameraActivity.newIntent(testContext, isPrivate)
            }
        val controller = Robolectric.buildActivity(LensCameraActivity::class.java, intent)
        controller.get().uploadViewModelFactory =
            LensUploadViewModel.Factory(
                uploaderProvider = { uploader },
                mainDispatcher = UnconfinedTestDispatcher(),
                backgroundDispatcher = UnconfinedTestDispatcher(),
            )
        if (savedState == null) controller.setup() else controller.setup(savedState)
        controller.get().supportFragmentManager.executePendingTransactions()
        return controller
    }

    @Test
    fun `GIVEN the opt-out sheet has not been acknowledged WHEN the activity reaches onResume THEN the sheet is shown and no camera fragment is added`() {
        every { settings.hasAcceptedGoogleLensFirstRun } returns false
        val shadowApp = Shadows.shadowOf(ApplicationProvider.getApplicationContext<Application>())
        shadowApp.grantPermissions(Manifest.permission.CAMERA)

        val activity = Robolectric.buildActivity(LensCameraActivity::class.java).setup().get()
        activity.supportFragmentManager.executePendingTransactions()

        assertNotNull(activity.supportFragmentManager.findFragmentByTag(GoogleLensOptOutBottomSheetFragment.TAG))
        assertNull(activity.supportFragmentManager.findFragmentById(R.id.lens_fragment_container_view))
    }

    @Test
    fun `GIVEN QR only and the opt-out sheet has not been acknowledged WHEN the activity reaches onResume THEN no sheet or backdrop is shown and the camera fragment is added`() {
        every { settings.hasAcceptedGoogleLensFirstRun } returns false
        val shadowApp = Shadows.shadowOf(ApplicationProvider.getApplicationContext<Application>())
        shadowApp.grantPermissions(Manifest.permission.CAMERA)
        val intent = LensCameraActivity.newQrScannerIntent(testContext)

        val activity = Robolectric.buildActivity(LensCameraActivity::class.java, intent).setup().get()
        Shadows.shadowOf(Looper.getMainLooper()).idle()
        activity.supportFragmentManager.executePendingTransactions()

        assertNull(activity.supportFragmentManager.findFragmentByTag(GoogleLensOptOutBottomSheetFragment.TAG))
        assertFalse(activity.findViewById<ComposeView>(R.id.lens_opt_out_backdrop).isVisible)
        assertIs<LensCameraFragment>(
            activity.supportFragmentManager.findFragmentById(R.id.lens_fragment_container_view)
        )
    }

    @Test
    fun `GIVEN the opt-out sheet has not been acknowledged WHEN requestOptOutBottomSheet is called THEN the sheet is not added before the first frame is drawn`() {
        every { settings.hasAcceptedGoogleLensFirstRun } returns false
        val activity = Robolectric.buildActivity(LensCameraActivity::class.java).create().get()

        activity.requestOptOutBottomSheet()
        activity.supportFragmentManager.executePendingTransactions()

        assertNull(activity.supportFragmentManager.findFragmentByTag(GoogleLensOptOutBottomSheetFragment.TAG))
    }

    @Test
    fun `GIVEN the opt-out sheet is already shown WHEN requestOptOutBottomSheet is called again THEN only one sheet is added`() {
        every { settings.hasAcceptedGoogleLensFirstRun } returns false
        val activity = Robolectric.buildActivity(LensCameraActivity::class.java).setup().get()

        activity.requestOptOutBottomSheet()
        activity.supportFragmentManager.executePendingTransactions()

        assertEquals(
            1,
            activity.supportFragmentManager.fragments.count { it is GoogleLensOptOutBottomSheetFragment },
        )
    }

    @Test
    fun `GIVEN the opt-out sheet is already shown WHEN showOptOutBottomSheet is called again THEN no duplicate sheet is added`() {
        every { settings.hasAcceptedGoogleLensFirstRun } returns false
        val activity = Robolectric.buildActivity(LensCameraActivity::class.java).setup().get()
        activity.supportFragmentManager.executePendingTransactions()
        val sheet = activity.supportFragmentManager.findFragmentByTag(GoogleLensOptOutBottomSheetFragment.TAG)

        activity.showOptOutBottomSheet()
        activity.supportFragmentManager.executePendingTransactions()

        assertSame(
            sheet,
            activity.supportFragmentManager.findFragmentByTag(GoogleLensOptOutBottomSheetFragment.TAG),
        )
    }

    @Test
    fun `GIVEN the opt-out sheet is shown WHEN its dialog is created THEN it is expanded and sized to its content`() {
        every { settings.hasAcceptedGoogleLensFirstRun } returns false
        val activity = Robolectric.buildActivity(LensCameraActivity::class.java).setup().get()
        activity.supportFragmentManager.executePendingTransactions()
        val sheet = activity.supportFragmentManager.findFragmentByTag(GoogleLensOptOutBottomSheetFragment.TAG)
        assertIs<GoogleLensOptOutBottomSheetFragment>(sheet)

        val behavior = assertIs<BottomSheetDialog>(sheet.dialog).behavior

        assertEquals(BottomSheetBehavior.STATE_EXPANDED, behavior.state)
        assertTrue(behavior.skipCollapsed)
        assertEquals(
            ViewGroup.LayoutParams.WRAP_CONTENT,
            sheet.requireView().layoutParams.height,
        )
    }

    @Test
    fun `GIVEN the opt-out sheet is shown WHEN it is cancelled without a choice THEN the activity returns RESULT_CANCELED and finishes`() {
        every { settings.hasAcceptedGoogleLensFirstRun } returns false
        val activity = Robolectric.buildActivity(LensCameraActivity::class.java).setup().get()
        activity.supportFragmentManager.executePendingTransactions()
        val sheet = activity.supportFragmentManager.findFragmentByTag(GoogleLensOptOutBottomSheetFragment.TAG)
        assertIs<GoogleLensOptOutBottomSheetFragment>(sheet)

        // A swipe, back press or tap outside all route through the dialog's cancel, which posts
        // its listener to the main looper rather than invoking it inline.
        sheet.dialog?.cancel()
        Shadows.shadowOf(Looper.getMainLooper()).idle()
        activity.supportFragmentManager.executePendingTransactions()

        assertEquals(Activity.RESULT_CANCELED, Shadows.shadowOf(activity).resultCode)
        assertTrue(activity.isFinishing)
    }

    @Test
    fun `GIVEN the opt-out sheet is shown WHEN the activity is recreated THEN the sheet survives and no choice is reported`() {
        every { settings.hasAcceptedGoogleLensFirstRun } returns false
        val controller = Robolectric.buildActivity(LensCameraActivity::class.java).setup()
        controller.get().supportFragmentManager.executePendingTransactions()

        val activity = controller.recreate().get()
        activity.supportFragmentManager.executePendingTransactions()

        // onDismiss also fires from onDestroyView on a configuration change; only onCancel means
        // the user actually declined, so a recreate must not cancel the activity.
        assertNotNull(activity.supportFragmentManager.findFragmentByTag(GoogleLensOptOutBottomSheetFragment.TAG))
        assertFalse(activity.isFinishing)
    }

    @Test
    // Robolectric grants declared permissions on request on SDK 37:
    // https://github.com/robolectric/robolectric/issues/11518
    @Config(sdk = [36])
    fun `GIVEN the camera permission was denied WHEN the activity resumes THEN the camera is not started behind the denial`() {
        // No grantPermissions here, so the resume inside setup requests the permission.
        val controller = Robolectric.buildActivity(LensCameraActivity::class.java).setup()
        val activity = controller.get()
        assertTrue(activity.permissionRequested)

        // Activity results are delivered before onResume, so the denial lands first and finishes us.
        activity.handlePermissionResult(isGranted = false)
        assertTrue(activity.isFinishing)

        // Were onResume to re-enter checkCameraPermission it would now find the permission granted
        // and start the camera the user just refused; a denial that stuck would re-prompt instead.
        Shadows.shadowOf(ApplicationProvider.getApplicationContext<Application>())
            .grantPermissions(Manifest.permission.CAMERA)
        controller.pause().resume()

        assertNull(activity.supportFragmentManager.findFragmentById(R.id.lens_fragment_container_view))
        assertEquals(Activity.RESULT_CANCELED, Shadows.shadowOf(activity).resultCode)
    }

    @Test
    fun `GIVEN the opt-out sheet is shown WHEN try it now leads to the camera THEN the backdrop is hidden`() {
        every { settings.hasAcceptedGoogleLensFirstRun } returns false
        val shadowApp = Shadows.shadowOf(ApplicationProvider.getApplicationContext<Application>())
        shadowApp.grantPermissions(Manifest.permission.CAMERA)
        val activity = Robolectric.buildActivity(LensCameraActivity::class.java).setup().get()
        val backdrop = activity.findViewById<ComposeView>(R.id.lens_opt_out_backdrop)
        assertTrue(backdrop.isVisible)

        activity.handleOptOutResult(GoogleLensOptOutBottomSheetFragment.ACTION_TRY_IT_NOW)
        activity.supportFragmentManager.executePendingTransactions()

        // Only covered by the fragment container, so it has to be gone rather than obscured or its
        // close button stays reachable by accessibility services.
        assertFalse(backdrop.isVisible)
    }

    @Test
    fun `GIVEN the opt-out sheet has been acknowledged WHEN the activity starts THEN the backdrop is never shown`() {
        val shadowApp = Shadows.shadowOf(ApplicationProvider.getApplicationContext<Application>())
        shadowApp.grantPermissions(Manifest.permission.CAMERA)

        val activity = Robolectric.buildActivity(LensCameraActivity::class.java).setup().get()

        assertFalse(activity.findViewById<ComposeView>(R.id.lens_opt_out_backdrop).isVisible)
    }

    @Test
    fun `WHEN the opt-out sheet reports try it now THEN the acknowledgement is recorded and the camera fragment is added`() {
        val shadowApp = Shadows.shadowOf(ApplicationProvider.getApplicationContext<Application>())
        shadowApp.grantPermissions(Manifest.permission.CAMERA)
        val activity = Robolectric.buildActivity(LensCameraActivity::class.java).create().get()

        activity.handleOptOutResult(GoogleLensOptOutBottomSheetFragment.ACTION_TRY_IT_NOW)
        activity.supportFragmentManager.executePendingTransactions()

        verify { settings.hasAcceptedGoogleLensFirstRun = true }
        assertIs<LensCameraFragment>(
            activity.supportFragmentManager.findFragmentById(R.id.lens_fragment_container_view)
        )
        assertFalse(activity.isFinishing)
    }

    @Test
    fun `WHEN the opt-out sheet reports not now THEN the activity returns RESULT_CANCELED and finishes`() {
        val activity = Robolectric.buildActivity(LensCameraActivity::class.java).create().get()

        activity.handleOptOutResult(GoogleLensOptOutBottomSheetFragment.ACTION_NOT_NOW)

        verify(exactly = 0) { settings.hasAcceptedGoogleLensFirstRun = true }
        assertEquals(Activity.RESULT_CANCELED, Shadows.shadowOf(activity).resultCode)
        assertTrue(activity.isFinishing)
    }

    @Test
    fun `WHEN the opt-out sheet is dismissed without an action THEN the activity returns RESULT_CANCELED and finishes`() {
        val activity = Robolectric.buildActivity(LensCameraActivity::class.java).create().get()

        activity.handleOptOutResult(null)

        assertEquals(Activity.RESULT_CANCELED, Shadows.shadowOf(activity).resultCode)
        assertTrue(activity.isFinishing)
    }

    @Test
    fun `WHEN the opt-out sheet reports the settings link THEN the Search settings deep link is started and the activity finishes`() {
        val activity = Robolectric.buildActivity(LensCameraActivity::class.java).create().get()

        activity.handleOptOutResult(GoogleLensOptOutBottomSheetFragment.ACTION_SETTINGS)

        val startedIntent = Shadows.shadowOf(activity).nextStartedActivity
        assertEquals(Intent.ACTION_VIEW, startedIntent.action)
        assertEquals(
            "${BuildConfig.DEEP_LINK_SCHEME}://settings_search_engine",
            startedIntent.data.toString(),
        )
        assertEquals(activity.packageName, startedIntent.`package`)
        verify(exactly = 0) { settings.hasAcceptedGoogleLensFirstRun = true }
        assertEquals(Activity.RESULT_CANCELED, Shadows.shadowOf(activity).resultCode)
        assertTrue(activity.isFinishing)
    }

    @Test
    @Config(sdk = [27], shadows = [ShadowBoundsReportingBitmapFactory::class])
    fun `GIVEN a decodable image WHEN the legacy decoder reads it THEN the null from the bounds pass is not a failure`() {
        val activity = Robolectric.buildActivity(LensCameraActivity::class.java).get()
        val uri = Uri.parse("content://media/external/images/1")
        Shadows.shadowOf(activity.contentResolver).registerInputStreamSupplier(uri) {
            ByteArrayInputStream(ByteArray(16))
        }

        val bitmap = activity.decodeDownsampledStream(uri)

        assertEquals(SHADOW_WIDTH, bitmap.width)
        assertEquals(SHADOW_HEIGHT, bitmap.height)
    }

    private companion object {
        const val LENS_RESULT_URL = "https://lens.google.com/results"
    }
}
