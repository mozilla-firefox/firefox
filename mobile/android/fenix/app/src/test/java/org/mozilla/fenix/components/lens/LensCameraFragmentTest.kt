/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.components.lens

import android.content.Context
import android.graphics.ImageFormat
import android.graphics.Insets
import android.graphics.Point
import android.graphics.Rect
import android.graphics.SurfaceTexture
import android.hardware.camera2.CameraAccessException
import android.hardware.camera2.CameraCaptureSession
import android.hardware.camera2.CameraDevice
import android.hardware.camera2.CameraManager
import android.hardware.camera2.CaptureRequest
import android.media.Image
import android.media.ImageReader
import android.net.Uri
import android.os.Build
import android.os.Bundle
import android.os.Handler
import android.os.HandlerThread
import android.util.Size
import android.view.Display
import android.view.Surface
import android.view.TextureView
import android.view.WindowInsets
import android.view.WindowManager
import android.view.WindowMetrics
import androidx.core.view.WindowInsetsCompat
import androidx.fragment.app.FragmentActivity
import androidx.fragment.app.FragmentManager
import io.mockk.Runs
import io.mockk.every
import io.mockk.just
import io.mockk.mockk
import io.mockk.spyk
import io.mockk.verify
import java.io.File
import java.nio.ByteBuffer
import java.util.concurrent.ExecutorService
import kotlin.test.assertNotNull
import mozilla.components.feature.qr.QrAnalyzer
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertSame
import org.junit.Assert.assertTrue
import org.junit.Assert.fail
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.annotation.Config
import org.robolectric.shadows.ShadowLooper

@RunWith(RobolectricTestRunner::class)
class LensCameraFragmentTest {

    // --- Lifecycle tests ---

    @Test
    fun `GIVEN fragment is pausing WHEN onPause is called THEN stopServices is invoked`() {
        val fragment = spyk(LensCameraFragment())
        every { fragment.stopServices() } just Runs

        fragment.onPause()

        verify { fragment.stopServices() }
    }

    @Test
    fun `GIVEN fragment is resuming WHEN onResume is called THEN startCamera is invoked`() {
        val fragment = spyk(LensCameraFragment())
        every { fragment.startCamera() } just Runs

        fragment.onResume()

        verify { fragment.startCamera() }
    }

    @Test
    fun `GIVEN textureView is available WHEN startCamera is called THEN tryOpenCamera is invoked`() {
        val fragment = spyk(LensCameraFragment())
        val textureView: TextureView = mockk(relaxed = true)
        every { textureView.isAvailable } returns true
        every { textureView.width } returns 1920
        every { textureView.height } returns 1080
        fragment.textureView = textureView
        every { fragment.maybeStartBackgroundThread() } just Runs
        every { fragment.maybeStartExecutorService() } just Runs
        every { fragment.tryOpenCamera(any(), any()) } just Runs

        fragment.startCamera()

        verify { fragment.maybeStartBackgroundThread() }
        verify { fragment.tryOpenCamera(1920, 1080) }
    }

    @Test
    fun `GIVEN textureView is unavailable WHEN startCamera is called THEN surfaceTextureListener is set`() {
        val fragment = spyk(LensCameraFragment())
        val textureView: TextureView = mockk(relaxed = true)
        every { textureView.isAvailable } returns false
        fragment.textureView = textureView
        every { fragment.maybeStartBackgroundThread() } just Runs
        every { fragment.maybeStartExecutorService() } just Runs

        fragment.startCamera()

        verify { textureView.surfaceTextureListener = any() }
    }

    // --- tryOpenCamera tests ---

    @Test
    fun `GIVEN no camera available WHEN tryOpenCamera is called THEN showCameraError is true`() {
        val fragment = spyk(LensCameraFragment())
        fragment.textureView = mockk(relaxed = true)

        every { fragment.deviceHasCamera() } returns false

        fragment.tryOpenCamera(0, 0)

        assertTrue(fragment.showCameraError.value)
    }

    @Test
    fun `GIVEN camera is available WHEN tryOpenCamera is called THEN showCameraError is false`() {
        val fragment = spyk(LensCameraFragment())
        fragment.textureView = mockk(relaxed = true)

        every { fragment.deviceHasCamera() } returns true
        every { fragment.openCamera(any(), any()) } just Runs

        fragment.tryOpenCamera(1920, 1080)

        verify { fragment.openCamera(1920, 1080) }
        assertFalse(fragment.showCameraError.value)
    }

    @Test
    fun `GIVEN camera throws exception WHEN tryOpenCamera is called THEN showCameraError is true`() {
        val fragment = spyk(LensCameraFragment())
        fragment.textureView = mockk(relaxed = true)

        every { fragment.deviceHasCamera() } returns true
        every { fragment.openCamera(any(), any()) } throws IllegalStateException("no camera")

        fragment.tryOpenCamera(0, 0)

        assertTrue(fragment.showCameraError.value)
    }

    // --- openCamera tests ---

    @Test
    fun `GIVEN camera throws CameraAccessException WHEN openCamera is called THEN exception is caught and handled`() {
        val fragment = spyk(LensCameraFragment())
        every { fragment.setUpCameraOutputs(any(), any()) } just Runs

        val cameraManager: CameraManager = mockk()
        every { cameraManager.openCamera(any<String>(), any<CameraDevice.StateCallback>(), any()) } throws
            CameraAccessException(CameraAccessException.CAMERA_ERROR)

        val activity: FragmentActivity = mockk()
        every { activity.getSystemService(Context.CAMERA_SERVICE) } returns cameraManager
        every { fragment.activity } returns activity
        fragment.cameraId = "mockCamera"

        try {
            fragment.openCamera(1920, 1080)
        } catch (e: CameraAccessException) {
            fail("CameraAccessException should have been caught and logged, not re-thrown.")
        }
    }

    @Test
    fun `GIVEN no camera ID is set WHEN openCamera is called THEN IllegalStateException is thrown`() {
        val fragment = spyk(LensCameraFragment())
        every { fragment.setUpCameraOutputs(any(), any()) } just Runs

        val cameraManager: CameraManager = mockk()
        val activity: FragmentActivity = mockk()
        every { activity.getSystemService(Context.CAMERA_SERVICE) } returns cameraManager
        every { fragment.activity } returns activity
        fragment.cameraId = null

        try {
            fragment.openCamera(1920, 1080)
            fail("Expected IllegalStateException")
        } catch (e: IllegalStateException) {
            assertEquals("No camera found on device", e.message)
        }
    }

    // --- stateCallback tests ---

    @Test
    fun `GIVEN camera device exists WHEN stateCallback onDisconnected is called THEN cameraDevice is null`() {
        val fragment = LensCameraFragment()
        fragment.cameraDevice = mockk(relaxed = true)

        fragment.stateCallback.onDisconnected(fragment.cameraDevice!!)

        assertNull(fragment.cameraDevice)
    }

    @Test
    fun `GIVEN camera device exists WHEN stateCallback onError is called THEN cameraDevice is null`() {
        val fragment = LensCameraFragment()
        fragment.cameraDevice = mockk(relaxed = true)

        fragment.stateCallback.onError(fragment.cameraDevice!!, 0)

        assertNull(fragment.cameraDevice)
    }

    // --- createCameraPreviewSession tests ---

    @Test
    fun `GIVEN previewSize is null WHEN createCameraPreviewSession is called THEN it returns early`() {
        val fragment = spyk(LensCameraFragment())
        fragment.cameraDevice = mockk(relaxed = true)

        val textureView: TextureView = mockk(relaxed = true)
        every { textureView.surfaceTexture } returns mockk(relaxed = true)
        fragment.textureView = textureView
        fragment.previewSize = null

        fragment.createCameraPreviewSession()
    }

    @Test
    fun `GIVEN imageReader surface is null WHEN createCameraPreviewSession is called THEN it does not crash`() {
        val fragment = spyk(LensCameraFragment())
        fragment.cameraDevice = mockk(relaxed = true)

        val imageReader: ImageReader = mockk()
        every { imageReader.surface } returns null
        fragment.imageReader = imageReader

        val textureView: TextureView = mockk(relaxed = true)
        every { textureView.surfaceTexture } returns mockk(relaxed = true)
        fragment.textureView = textureView

        fragment.previewSize = Size(1920, 1080)

        try {
            fragment.createCameraPreviewSession()
        } catch (e: NullPointerException) {
            fail("NullPointerException should not have been thrown.")
        }
    }

    // --- onSessionConfigureFailed tests ---

    @Test
    fun `GIVEN the capture session fails to configure WHEN onSessionConfigureFailed is called THEN showCameraError is true`() {
        val fragment = LensCameraFragment()
        assertFalse(fragment.showCameraError.value)

        fragment.onSessionConfigureFailed()
        ShadowLooper.idleMainLooper()

        assertTrue(fragment.showCameraError.value)
    }

    // --- onSessionConfigured tests ---

    @Test
    fun `GIVEN cameraDevice is null WHEN onSessionConfigured is called THEN the session is closed and setRepeatingRequest is not called`() {
        val fragment = LensCameraFragment()
        fragment.cameraDevice = null

        val session: CameraCaptureSession = mockk(relaxed = true)
        val request: CaptureRequest = mockk(relaxed = true)
        val captureCallback = object : CameraCaptureSession.CaptureCallback() {}

        fragment.onSessionConfigured(session, request, captureCallback)

        verify { session.close() }
        verify(exactly = 0) { session.setRepeatingRequest(any(), any(), any<Handler>()) }
        assertNull(fragment.captureSession)
    }

    @Test
    fun `GIVEN cameraDevice exists WHEN onSessionConfigured is called THEN setRepeatingRequest is called and captureSession is assigned`() {
        val fragment = LensCameraFragment()
        fragment.cameraDevice = mockk(relaxed = true)

        val session: CameraCaptureSession = mockk(relaxed = true)
        val request: CaptureRequest = mockk(relaxed = true)
        val captureCallback = object : CameraCaptureSession.CaptureCallback() {}

        fragment.onSessionConfigured(session, request, captureCallback)

        assertSame(session, fragment.captureSession)
        verify { session.setRepeatingRequest(request, captureCallback, any<Handler>()) }
    }

    @Test
    fun `GIVEN setRepeatingRequest throws IllegalArgumentException WHEN onSessionConfigured is called THEN it does not crash`() {
        val fragment = LensCameraFragment()
        fragment.cameraDevice = mockk(relaxed = true)

        val session: CameraCaptureSession = mockk(relaxed = true)
        every { session.setRepeatingRequest(any(), any(), any<Handler>()) } throws
            IllegalArgumentException("CaptureRequest contains unconfigured Input/Output Surface!")
        val request: CaptureRequest = mockk(relaxed = true)
        val captureCallback = object : CameraCaptureSession.CaptureCallback() {}

        try {
            fragment.onSessionConfigured(session, request, captureCallback)
        } catch (e: IllegalArgumentException) {
            fail("IllegalArgumentException should have been caught, not propagated.")
        }
    }

    // --- chooseOptimalSize tests ---

    @Test(expected = IllegalArgumentException::class)
    fun `GIVEN empty size array WHEN chooseOptimalSize is called THEN IllegalArgumentException is thrown`() {
        LensCameraFragment.chooseOptimalSize(emptyArray(), 640, 480, 1920, 1080, Size(4, 3))
    }

    @Test
    fun `GIVEN big-enough sizes with matching aspect ratio WHEN chooseOptimalSize is called THEN smallest matching size is returned`() {
        val size =
            LensCameraFragment.chooseOptimalSize(
                arrayOf(Size(640, 480), Size(1024, 768)),
                640,
                480,
                1920,
                1080,
                Size(4, 3),
            )

        assertEquals(640, size.width)
        assertEquals(480, size.height)
    }

    @Test
    fun `GIVEN no big-enough sizes WHEN chooseOptimalSize is called THEN largest not-big-enough size is returned`() {
        val size =
            LensCameraFragment.chooseOptimalSize(
                arrayOf(Size(320, 240), Size(640, 480)),
                1024,
                768,
                1920,
                1080,
                Size(4, 3),
            )

        assertEquals(640, size.width)
        assertEquals(480, size.height)
    }

    @Test
    fun `GIVEN no exact aspect ratio match WHEN chooseOptimalSize is called THEN closest available ratio is returned`() {
        val size =
            LensCameraFragment.chooseOptimalSize(
                arrayOf(Size(1024, 768), Size(786, 480)),
                2048,
                1024,
                1920,
                1080,
                Size(16, 9),
            )

        assertEquals(786, size.width)
        assertEquals(480, size.height)
    }

    @Test
    fun `GIVEN a capture size that is only approximately 4-3 WHEN chooseOptimalSize is called THEN the largest 4-3 preview size is returned`() {
        val size =
            LensCameraFragment.chooseOptimalSize(
                arrayOf(Size(320, 240), Size(640, 480), Size(1280, 960), Size(1440, 1080)),
                2400,
                1080,
                1920,
                1080,
                // Pixel 7 rear camera reports 4080x3072, a ratio of 1.328 rather than 1.333.
                Size(4080, 3072),
            )

        assertEquals(1440, size.width)
        assertEquals(1080, size.height)
    }

    @Test
    fun `GIVEN a Samsung style capture size WHEN chooseOptimalSize is called THEN the largest 4-3 preview size is returned`() {
        val size =
            LensCameraFragment.chooseOptimalSize(
                arrayOf(Size(640, 480), Size(1440, 1080)),
                2400,
                1080,
                1920,
                1080,
                Size(4624, 3472),
            )

        assertEquals(1440, size.width)
        assertEquals(1080, size.height)
    }

    @Test
    fun `GIVEN a size just outside the ratio tolerance WHEN chooseOptimalSize is called THEN it is not treated as matching`() {
        // 1600x1080 is 1.481, which is 0.148 away from the closest candidate's ratio and well outside the tolerance.
        val size =
            LensCameraFragment.chooseOptimalSize(
                arrayOf(Size(1440, 1080), Size(1600, 1080)),
                2400,
                1080,
                1920,
                1080,
                Size(4, 3),
            )

        assertEquals(1440, size.width)
        assertEquals(1080, size.height)
    }

    @Test
    fun `GIVEN sizes exceeding max dimensions WHEN chooseOptimalSize is called THEN oversized entries are filtered out`() {
        val size =
            LensCameraFragment.chooseOptimalSize(
                arrayOf(Size(2560, 1920), Size(1024, 768), Size(640, 480)),
                640,
                480,
                1920,
                1080,
                Size(4, 3),
            )

        assertEquals(640, size.width)
        assertEquals(480, size.height)
    }

    @Test
    fun `GIVEN no size fits the preview bounds WHEN chooseOptimalSize is called THEN the smallest size is returned`() {
        // Never the largest: an oversized preview stream can fail the whole capture session on LEGACY devices.
        val size =
            LensCameraFragment.chooseOptimalSize(
                arrayOf(Size(4608, 3456), Size(2560, 1920)),
                640,
                480,
                1920,
                1080,
                Size(4, 3),
            )

        assertEquals(2560, size.width)
        assertEquals(1920, size.height)
    }

    // --- chooseCaptureSize tests ---

    @Test
    fun `GIVEN several JPEG sizes WHEN chooseCaptureSize is called THEN the sensor maximum is returned`() {
        val size = LensCameraFragment.chooseCaptureSize(arrayOf(Size(1920, 1080), Size(4608, 3456), Size(640, 480)))

        assertEquals(4608, size.width)
        assertEquals(3456, size.height)
    }

    @Test
    fun `GIVEN a maximum above the old dimension cap WHEN chooseCaptureSize is called THEN it is still returned`() {
        // Capping the capture size takes the three-stream request outside camera2's guaranteed combinations.
        val size = LensCameraFragment.chooseCaptureSize(arrayOf(Size(4608, 3456), Size(3840, 2160)))

        assertEquals(4608, size.width)
        assertEquals(3456, size.height)
    }

    @Test(expected = IllegalArgumentException::class)
    fun `GIVEN empty size array WHEN chooseCaptureSize is called THEN IllegalArgumentException is thrown`() {
        LensCameraFragment.chooseCaptureSize(emptyArray())
    }

    // --- chooseQrSize tests ---

    @Test
    fun `GIVEN supported YUV sizes WHEN chooseQrSize is called THEN the one closest to the analyzer target is returned`() {
        val size =
            LensCameraFragment.chooseQrSize(
                arrayOf(Size(1440, 1080), Size(800, 600), Size(176, 144)),
                1920,
                1080,
                Size(1440, 1080),
            )

        // 800x600 is nearest QrAnalyzer's 786x786 target area.
        assertEquals(800, size.width)
        assertEquals(600, size.height)
    }

    @Test
    fun `GIVEN YUV sizes above the preview bounds WHEN chooseQrSize is called THEN an in-bounds size is returned`() {
        val size =
            LensCameraFragment.chooseQrSize(
                arrayOf(Size(4608, 3456), Size(640, 480)),
                1920,
                1080,
                Size(1440, 1080),
            )

        assertEquals(640, size.width)
        assertEquals(480, size.height)
    }

    @Test
    fun `GIVEN no YUV size fits the preview bounds WHEN chooseQrSize is called THEN the smallest size is returned`() {
        val size =
            LensCameraFragment.chooseQrSize(
                arrayOf(Size(4608, 3456), Size(2560, 1920)),
                1920,
                1080,
                Size(1440, 1080),
            )

        assertEquals(2560, size.width)
        assertEquals(1920, size.height)
    }

    @Test
    fun `GIVEN a closer size of the wrong ratio WHEN chooseQrSize is called THEN the one matching the preview is returned`() {
        val size =
            LensCameraFragment.chooseQrSize(
                arrayOf(Size(800, 800), Size(1024, 768)),
                1920,
                1080,
                Size(1440, 1080),
            )

        // 800x800 is 640000 px, far nearer the 617796 px target than 1024x768's 786432, but it is not 4:3.
        assertEquals(1024, size.width)
        assertEquals(768, size.height)
    }

    @Test
    fun `GIVEN a 16-9 preview WHEN chooseQrSize is called THEN a 16-9 YUV size is returned`() {
        val size =
            LensCameraFragment.chooseQrSize(
                arrayOf(Size(1024, 768), Size(1280, 720)),
                1920,
                1080,
                Size(1920, 1080),
            )

        assertEquals(1280, size.width)
        assertEquals(720, size.height)
    }

    @Test
    fun `GIVEN the camera reports no YUV sizes WHEN chooseQrSize is called THEN the analyzer default is returned`() {
        val size = LensCameraFragment.chooseQrSize(null, 1920, 1080, Size(1440, 1080))

        assertEquals(QrAnalyzer.YUV_WIDTH, size.width)
        assertEquals(QrAnalyzer.YUV_HEIGHT, size.height)
    }

    // --- getDisplaySize tests ---

    @Test
    @Suppress("DEPRECATION")
    @Config(sdk = [Build.VERSION_CODES.Q])
    fun `GIVEN SDK is below 30 WHEN getDisplaySize is called THEN defaultDisplay getSize is used`() {
        val mockManager: WindowManager = mockk()
        val mockDisplay: Display = mockk()

        every { mockManager.defaultDisplay } returns mockDisplay
        every { mockDisplay.getSize(any()) } just Runs

        LensCameraFragment.getDisplaySize(mockManager)

        verify { mockDisplay.getSize(any()) }
    }

    @Test
    fun `GIVEN SDK is 30 or above WHEN getDisplaySize is called THEN currentWindowMetrics is used`() {
        val mockManager: WindowManager = mockk()
        val mockWindowMetrics: WindowMetrics = mockk()

        val bounds = Rect(0, 0, 170, 270)
        val insets = Insets.of(10, 10, 10, 10)
        val expectedSize = Point(150, 250)

        val mockWindowInsets: WindowInsets = mockk()
        every {
            mockWindowInsets.getInsetsIgnoringVisibility(
                WindowInsetsCompat.Type.navigationBars() or WindowInsetsCompat.Type.displayCutout()
            )
        } returns insets

        every { mockManager.currentWindowMetrics } returns mockWindowMetrics
        every { mockWindowMetrics.windowInsets } returns mockWindowInsets
        every { mockWindowMetrics.bounds } returns bounds

        val result = LensCameraFragment.getDisplaySize(mockManager)

        assertEquals(expectedSize, result)
    }

    // --- getScreenRotation tests ---

    @Test
    fun `GIVEN SDK is 30 or above WHEN getScreenRotation is called THEN context display is used`() {
        val fragment = spyk(LensCameraFragment())
        val mockContext: FragmentActivity = mockk()
        val mockDisplay: Display = mockk()

        every { fragment.context } returns mockContext
        every { mockContext.display } returns mockDisplay
        every { mockDisplay.rotation } returns Surface.ROTATION_90

        val rotation = fragment.getScreenRotation()

        assertEquals(Surface.ROTATION_90, rotation)
    }

    @Test
    @Suppress("DEPRECATION")
    @Config(sdk = [Build.VERSION_CODES.Q])
    fun `GIVEN SDK is below 30 WHEN getScreenRotation is called THEN windowManager defaultDisplay is used`() {
        val fragment = spyk(LensCameraFragment())
        val mockActivity: FragmentActivity = mockk()
        val mockManager: WindowManager = mockk()
        val mockDisplay: Display = mockk()

        every { fragment.context } returns null
        every { fragment.activity } returns mockActivity
        every { mockActivity.windowManager } returns mockManager
        every { mockManager.defaultDisplay } returns mockDisplay
        every { mockDisplay.rotation } returns Surface.ROTATION_90

        val rotation = fragment.getScreenRotation()

        assertEquals(Surface.ROTATION_90, rotation)
    }

    // --- configureTransform tests ---

    @Test
    fun `GIVEN previewSize is set WHEN configureTransform is called THEN the transform is applied to the textureView`() {
        val fragment = spyk(LensCameraFragment())
        val textureView: TextureView = mockk(relaxed = true)
        every { fragment.getScreenRotation() } returns Surface.ROTATION_0
        fragment.textureView = textureView
        fragment.previewSize = Size(1920, 1080)

        fragment.configureTransform(viewWidth = 1080, viewHeight = 2400)

        verify { textureView.setTransform(any()) }
    }

    @Test
    fun `GIVEN previewSize is null WHEN configureTransform is called THEN no transform is applied`() {
        val fragment = spyk(LensCameraFragment())
        val textureView: TextureView = mockk(relaxed = true)
        fragment.textureView = textureView
        fragment.previewSize = null

        fragment.configureTransform(viewWidth = 1080, viewHeight = 2400)

        verify(exactly = 0) { textureView.setTransform(any()) }
    }

    // --- Background thread and executor tests ---

    @Test
    fun `GIVEN background thread is already alive WHEN maybeStartBackgroundThread is called THEN existing thread is reused`() {
        val fragment = LensCameraFragment()
        val existingThread = HandlerThread("test").apply { start() }
        val existingHandler: Handler = mockk()
        fragment.backgroundThread = existingThread
        fragment.backgroundHandler = existingHandler

        fragment.maybeStartBackgroundThread()

        assertSame(existingThread, fragment.backgroundThread)
        assertSame(existingHandler, fragment.backgroundHandler)

        existingThread.quitSafely()
        existingThread.join()
    }

    @Test
    fun `GIVEN background thread is null WHEN maybeStartBackgroundThread is called THEN new thread is created`() {
        val fragment = LensCameraFragment()
        fragment.backgroundThread = null
        fragment.backgroundHandler = null

        fragment.maybeStartBackgroundThread()

        assertNotNull(fragment.backgroundThread)
        assertTrue(fragment.backgroundThread!!.isAlive)
        assertNotNull(fragment.backgroundHandler)

        fragment.backgroundThread?.quitSafely()
        fragment.backgroundThread?.join()
    }

    @Test
    fun `GIVEN background thread is running WHEN stopBackgroundThread is called THEN fields are cleared`() {
        val fragment = LensCameraFragment()
        val thread = HandlerThread("test").apply { start() }
        fragment.backgroundThread = thread
        fragment.backgroundHandler = Handler(thread.looper)

        fragment.stopBackgroundThread()

        assertNull(fragment.backgroundThread)
        assertNull(fragment.backgroundHandler)
    }

    @Test
    fun `GIVEN join is interrupted WHEN stopBackgroundThread is called THEN fields are still cleared`() {
        val fragment = LensCameraFragment()
        val thread = HandlerThread("test").apply { start() }
        fragment.backgroundThread = thread
        fragment.backgroundHandler = Handler(thread.looper)

        try {
            Thread.currentThread().interrupt()
            fragment.stopBackgroundThread()

            assertNull(fragment.backgroundThread)
            assertNull(fragment.backgroundHandler)

            thread.quitSafely()
            thread.join()

            // After clearing, maybeStartBackgroundThread must produce a fresh, started thread
            // rather than re-using a dead reference (which would throw IllegalThreadStateException).
            fragment.maybeStartBackgroundThread()
            assertNotNull(fragment.backgroundThread)
            assertTrue(fragment.backgroundThread!!.isAlive)
            fragment.backgroundThread?.quitSafely()
            fragment.backgroundThread?.join()
        } finally {
            // Clear interrupted flag so a failed assertion doesn't leak it to subsequent tests.
            Thread.interrupted()
        }
    }

    @Test
    fun `GIVEN executor service already exists WHEN maybeStartExecutorService is called THEN existing executor is reused`() {
        val fragment = LensCameraFragment()
        val existingExecutor: ExecutorService = mockk()
        fragment.backgroundExecutor = existingExecutor

        fragment.maybeStartExecutorService()

        assertSame(existingExecutor, fragment.backgroundExecutor)
    }

    @Test
    fun `GIVEN executor service is null WHEN maybeStartExecutorService is called THEN new executor is created`() {
        val fragment = LensCameraFragment()
        fragment.backgroundExecutor = null

        fragment.maybeStartExecutorService()

        assertNotNull(fragment.backgroundExecutor)
    }

    // --- onImageAvailableListener tests ---

    @Test
    fun `GIVEN disk write fails WHEN onImageAvailableListener processes image THEN handleResult is called with null`() {
        val fragment = spyk(LensCameraFragment())
        every { fragment.handleResult(any()) } just Runs

        val mockContext: Context = mockk()
        val readOnlyDir = File("/non_existent_read_only_path")
        every { mockContext.applicationContext } returns mockContext
        every { mockContext.cacheDir } returns readOnlyDir
        every { mockContext.packageName } returns "org.mozilla.fenix"
        every { fragment.context } returns mockContext

        val buffer = ByteBuffer.wrap(byteArrayOf(1, 2, 3))
        val mockPlane: Image.Plane = mockk()
        every { mockPlane.buffer } returns buffer

        val mockImage: Image = mockk()
        every { mockImage.planes } returns arrayOf(mockPlane)
        every { mockImage.close() } just Runs

        val mockReader: ImageReader = mockk()
        every { mockReader.acquireLatestImage() } returns mockImage

        fragment.processImage(mockReader)
        ShadowLooper.idleMainLooper()

        verify { fragment.handleResult(null) }
        verify { mockImage.close() }
    }

    @Test
    fun `GIVEN image write succeeds WHEN onImageAvailableListener processes image THEN handleResult is called with URI`() {
        val fragment = spyk(LensCameraFragment())
        every { fragment.handleResult(any()) } just Runs

        val tempDir = File(System.getProperty("java.io.tmpdir"), "lens_test_${System.currentTimeMillis()}")
        tempDir.mkdirs()

        val expectedUri: Uri = mockk()
        fragment.getUriForFile = { _, _, _ -> expectedUri }

        val mockContext: Context = mockk()
        every { mockContext.applicationContext } returns mockContext
        every { mockContext.cacheDir } returns tempDir
        every { mockContext.packageName } returns "org.mozilla.fenix"
        every { fragment.context } returns mockContext

        val buffer = ByteBuffer.wrap(byteArrayOf(1, 2, 3))
        val mockPlane: Image.Plane = mockk()
        every { mockPlane.buffer } returns buffer

        val mockImage: Image = mockk()
        every { mockImage.planes } returns arrayOf(mockPlane)
        every { mockImage.close() } just Runs

        val mockReader: ImageReader = mockk()
        every { mockReader.acquireLatestImage() } returns mockImage

        fragment.processImage(mockReader)
        ShadowLooper.idleMainLooper()

        verify { fragment.handleResult(expectedUri) }
        verify { mockImage.close() }

        tempDir.deleteRecursively()
    }

    @Test
    fun `GIVEN a fixed clock WHEN processImage writes the capture THEN the filename contains the timestamp`() {
        val fragment = spyk(LensCameraFragment(now = { 1234567890L }))
        every { fragment.handleResult(any()) } just Runs

        val tempDir = File(System.getProperty("java.io.tmpdir"), "lens_test_${System.nanoTime()}")
        tempDir.mkdirs()

        var capturedFile: File? = null
        fragment.getUriForFile = { _, _, file ->
            capturedFile = file
            mockk()
        }

        val mockContext: Context = mockk()
        every { mockContext.applicationContext } returns mockContext
        every { mockContext.cacheDir } returns tempDir
        every { mockContext.packageName } returns "org.mozilla.fenix"
        every { fragment.context } returns mockContext

        val buffer = ByteBuffer.wrap(byteArrayOf(1, 2, 3))
        val mockPlane: Image.Plane = mockk()
        every { mockPlane.buffer } returns buffer

        val mockImage: Image = mockk()
        every { mockImage.planes } returns arrayOf(mockPlane)
        every { mockImage.close() } just Runs

        val mockReader: ImageReader = mockk()
        every { mockReader.acquireLatestImage() } returns mockImage

        fragment.processImage(mockReader)
        ShadowLooper.idleMainLooper()

        assertEquals("lens_capture_1234567890.jpg", capturedFile?.name)

        tempDir.deleteRecursively()
    }

    // --- cameraMode and QR scanning tests ---

    @Test
    fun `WHEN a fragment is created THEN cameraMode defaults to LENS`() {
        val fragment = LensCameraFragment()

        assertEquals(CameraMode.LENS, fragment.cameraMode.value)
    }

    @Test
    fun `GIVEN cameraMode is LENS WHEN qrImageAvailableListener fires THEN the image is drained without invoking the analyzer`() {
        val fragment = spyk(LensCameraFragment())
        val analyzer: QrAnalyzer = mockk()
        fragment.qrAnalyzer = analyzer
        fragment.cameraMode.value = CameraMode.LENS

        val mockImage: Image = mockk(relaxed = true)
        val mockReader: ImageReader = mockk()
        every { mockReader.acquireLatestImage() } returns mockImage
        every { mockImage.close() } just Runs

        fragment.qrImageAvailableListener.onImageAvailable(mockReader)

        verify { mockImage.close() }
        verify(exactly = 0) { analyzer.analyze(any<Image>()) }
        verify(exactly = 0) { fragment.handleQrResult(any()) }
    }

    @Test
    fun `GIVEN cameraMode is QR AND analyzer returns a string WHEN qrImageAvailableListener fires THEN handleQrResult is posted`() {
        val fragment = LensCameraFragment()
        val analyzer: QrAnalyzer = mockk()
        every { analyzer.analyze(any<Image>()) } returns "https://example.com"
        fragment.qrAnalyzer = analyzer
        fragment.cameraMode.value = CameraMode.QR

        val mockImage: Image = mockk(relaxed = true)
        val mockReader: ImageReader = mockk()
        every { mockReader.acquireLatestImage() } returns mockImage
        every { mockImage.close() } just Runs

        fragment.qrImageAvailableListener.onImageAvailable(mockReader)
        ShadowLooper.idleMainLooper()

        assertTrue(fragment.qrResultSent)
        verify { mockImage.close() }
    }

    @Test
    fun `GIVEN cameraMode is QR AND analyzer returns null WHEN qrImageAvailableListener fires THEN handleQrResult is not invoked`() {
        val fragment = LensCameraFragment()
        val analyzer: QrAnalyzer = mockk()
        every { analyzer.analyze(any<Image>()) } returns null
        fragment.qrAnalyzer = analyzer
        fragment.cameraMode.value = CameraMode.QR

        val mockImage: Image = mockk(relaxed = true)
        val mockReader: ImageReader = mockk()
        every { mockReader.acquireLatestImage() } returns mockImage
        every { mockImage.close() } just Runs

        fragment.qrImageAvailableListener.onImageAvailable(mockReader)
        ShadowLooper.idleMainLooper()

        assertFalse(fragment.qrResultSent)
        verify { mockImage.close() }
    }

    @Test
    fun `GIVEN cameraMode is QR AND qrInFlight is already true WHEN qrImageAvailableListener fires THEN the frame is not acquired`() {
        val fragment = LensCameraFragment()
        fragment.cameraMode.value = CameraMode.QR
        fragment.qrInFlight.set(true)

        val mockReader: ImageReader = mockk(relaxed = true)

        fragment.qrImageAvailableListener.onImageAvailable(mockReader)

        verify(exactly = 0) { mockReader.acquireLatestImage() }
        assertTrue(fragment.qrInFlight.get())
    }

    @Test
    fun `GIVEN qrResultSent is false WHEN handleQrResult is called THEN qrResultSent becomes true`() {
        val fragment = LensCameraFragment()
        fragment.cameraMode.value = CameraMode.QR
        assertFalse(fragment.qrResultSent)

        fragment.handleQrResult("https://example.com")

        assertTrue(fragment.qrResultSent)
    }

    @Test
    fun `GIVEN qrResultSent is already true WHEN handleQrResult is called again THEN it remains a no-op`() {
        val fragment = LensCameraFragment()
        fragment.cameraMode.value = CameraMode.QR
        fragment.qrResultSent = true

        // Second call must not throw and must not flip any state.
        fragment.handleQrResult("https://example.com")

        assertTrue(fragment.qrResultSent)
    }

    @Test
    fun `GIVEN cameraMode is LENS WHEN handleQrResult is called THEN it is a no-op`() {
        val fragment = LensCameraFragment()
        fragment.cameraMode.value = CameraMode.LENS
        assertFalse(fragment.qrResultSent)

        fragment.handleQrResult("https://example.com")

        assertFalse(fragment.qrResultSent)
    }

    // --- previewTargets / updatePreviewRequest tests ---

    @Test
    fun `GIVEN cameraMode is LENS WHEN previewTargets is called THEN only the preview surface is targeted`() {
        val fragment = LensCameraFragment()
        fragment.cameraMode.value = CameraMode.LENS
        val previewSurface = Surface(SurfaceTexture(0))
        val qrSurface = Surface(SurfaceTexture(1))

        assertEquals(listOf(previewSurface), fragment.previewTargets(previewSurface, qrSurface))
    }

    @Test
    fun `GIVEN cameraMode is QR WHEN previewTargets is called THEN the QR surface is also targeted`() {
        val fragment = LensCameraFragment()
        fragment.cameraMode.value = CameraMode.QR
        val previewSurface = Surface(SurfaceTexture(0))
        val qrSurface = Surface(SurfaceTexture(1))

        assertEquals(listOf(previewSurface, qrSurface), fragment.previewTargets(previewSurface, qrSurface))
    }

    @Test
    fun `GIVEN no capture session WHEN updatePreviewRequest is called THEN nothing happens`() {
        val fragment = LensCameraFragment()
        fragment.captureSession = null
        fragment.cameraDevice = mockk(relaxed = true)

        fragment.updatePreviewRequest()
    }

    @Test
    fun `GIVEN a live session WHEN updatePreviewRequest is called THEN the repeating request is replaced`() {
        val request: CaptureRequest = mockk(relaxed = true)
        val fragment = spyk(LensCameraFragment())
        every { fragment.buildPreviewRequest(any(), any(), any()) } returns request

        val session: CameraCaptureSession = mockk(relaxed = true)
        fragment.captureSession = session
        fragment.cameraDevice = mockk(relaxed = true)
        fragment.surface = Surface(SurfaceTexture(0))
        fragment.qrImageReader = ImageReader.newInstance(64, 64, ImageFormat.YUV_420_888, 2)

        fragment.updatePreviewRequest()

        verify { session.setRepeatingRequest(request, null, any()) }
    }

    @Test
    fun `GIVEN setRepeatingRequest throws IllegalArgumentException WHEN updatePreviewRequest is called THEN it does not crash`() {
        val fragment = spyk(LensCameraFragment())
        every { fragment.buildPreviewRequest(any(), any(), any()) } returns mockk(relaxed = true)

        val session: CameraCaptureSession = mockk(relaxed = true)
        every { session.setRepeatingRequest(any(), any(), any<Handler>()) } throws
            IllegalArgumentException("CaptureRequest contains unconfigured Input/Output Surface!")
        fragment.captureSession = session
        fragment.cameraDevice = mockk(relaxed = true)
        fragment.surface = Surface(SurfaceTexture(0))
        fragment.qrImageReader = ImageReader.newInstance(64, 64, ImageFormat.YUV_420_888, 2)

        try {
            fragment.updatePreviewRequest()
        } catch (e: IllegalArgumentException) {
            fail("IllegalArgumentException should have been caught, not propagated.")
        }
    }

    @Test
    fun `GIVEN a mode change WHEN handleModeChanged is called THEN the repeating request is rebuilt`() {
        val fragment = spyk(LensCameraFragment())
        every { fragment.updatePreviewRequest() } just Runs
        fragment.cameraMode.value = CameraMode.LENS

        fragment.handleModeChanged(CameraMode.QR)

        verify { fragment.updatePreviewRequest() }
    }

    // --- handleModeChanged tests ---

    @Test
    fun `GIVEN cameraMode is LENS WHEN handleModeChanged QR is called THEN analyzer is reset and qrResultSent is cleared`() {
        val fragment = LensCameraFragment()
        val analyzer: QrAnalyzer = mockk(relaxed = true)
        fragment.qrAnalyzer = analyzer
        fragment.cameraMode.value = CameraMode.LENS
        fragment.qrResultSent = true

        fragment.handleModeChanged(CameraMode.QR)

        assertEquals(CameraMode.QR, fragment.cameraMode.value)
        assertFalse(fragment.qrResultSent)
        verify { analyzer.reset() }
    }

    @Test
    fun `GIVEN cameraMode is QR WHEN handleModeChanged LENS is called THEN analyzer is not reset`() {
        val fragment = LensCameraFragment()
        val analyzer: QrAnalyzer = mockk(relaxed = true)
        fragment.qrAnalyzer = analyzer
        fragment.cameraMode.value = CameraMode.QR

        fragment.handleModeChanged(CameraMode.LENS)

        assertEquals(CameraMode.LENS, fragment.cameraMode.value)
        verify(exactly = 0) { analyzer.reset() }
    }

    @Test
    fun `GIVEN mode is unchanged WHEN handleModeChanged is called THEN analyzer is not reset and qrResultSent is preserved`() {
        val fragment = LensCameraFragment()
        val analyzer: QrAnalyzer = mockk(relaxed = true)
        fragment.qrAnalyzer = analyzer
        fragment.cameraMode.value = CameraMode.QR
        fragment.qrResultSent = true

        fragment.handleModeChanged(CameraMode.QR)

        assertTrue(fragment.qrResultSent)
        verify(exactly = 0) { analyzer.reset() }
    }

    @Test
    fun `GIVEN the fragment is added WHEN handleModeChanged switches the mode THEN the new mode is reported as a fragment result`() {
        val fragment = spyk(LensCameraFragment())
        val fragmentManager: FragmentManager = mockk(relaxed = true)
        every { fragment.isAdded } returns true
        every { fragment.parentFragmentManager } returns fragmentManager
        every { fragment.updatePreviewRequest() } just Runs
        fragment.cameraMode.value = CameraMode.LENS

        fragment.handleModeChanged(CameraMode.QR)

        verify {
            fragmentManager.setFragmentResult(
                LensCameraFragment.MODE_CHANGED_REQUEST_KEY,
                match { it.getString(LensCameraFragment.RESULT_CAMERA_MODE) == "QR" },
            )
        }
    }

    @Test
    fun `GIVEN the fragment is added WHEN handleModeChanged keeps the same mode THEN no fragment result is reported`() {
        val fragment = spyk(LensCameraFragment())
        val fragmentManager: FragmentManager = mockk(relaxed = true)
        every { fragment.isAdded } returns true
        every { fragment.parentFragmentManager } returns fragmentManager
        fragment.cameraMode.value = CameraMode.QR

        fragment.handleModeChanged(CameraMode.QR)

        verify(exactly = 0) { fragmentManager.setFragmentResult(any(), any()) }
    }

    @Test
    fun `GIVEN QR only WHEN handleModeChanged LENS is called THEN the mode stays QR and no fragment result is reported`() {
        val fragment = spyk(LensCameraFragment())
        val fragmentManager: FragmentManager = mockk(relaxed = true)
        every { fragment.isAdded } returns true
        every { fragment.parentFragmentManager } returns fragmentManager
        fragment.arguments = Bundle().apply { putBoolean(LensCameraFragment.ARG_QR_ONLY, true) }
        fragment.cameraMode.value = CameraMode.QR

        fragment.handleModeChanged(CameraMode.LENS)

        assertEquals(CameraMode.QR, fragment.cameraMode.value)
        verify(exactly = 0) { fragmentManager.setFragmentResult(any(), any()) }
    }

    // --- onSaveInstanceState / onCreate tests ---

    @Test
    fun `GIVEN cameraMode is QR WHEN onSaveInstanceState is called THEN the mode name is written to the bundle`() {
        val fragment = LensCameraFragment()
        fragment.cameraMode.value = CameraMode.QR
        val outState = Bundle()

        fragment.onSaveInstanceState(outState)

        assertEquals("QR", outState.getString("camera_mode"))
    }

    @Test
    fun `GIVEN savedInstanceState contains QR WHEN restoreFromState is called THEN cameraMode is restored to QR`() {
        val fragment = LensCameraFragment()
        val savedState = Bundle().apply { putString("camera_mode", "QR") }

        fragment.restoreFromState(savedState)

        assertEquals(CameraMode.QR, fragment.cameraMode.value)
    }

    @Test
    fun `GIVEN savedInstanceState is null WHEN restoreFromState is called THEN cameraMode stays at LENS default`() {
        val fragment = LensCameraFragment()

        fragment.restoreFromState(null)

        assertEquals(CameraMode.LENS, fragment.cameraMode.value)
    }

    @Test
    fun `GIVEN savedInstanceState is null AND an initial mode argument of QR WHEN restoreFromState is called THEN cameraMode is QR`() {
        val fragment = LensCameraFragment()
        fragment.arguments = Bundle().apply { putString(LensCameraFragment.ARG_INITIAL_MODE, "QR") }

        fragment.restoreFromState(null)

        assertEquals(CameraMode.QR, fragment.cameraMode.value)
    }

    @Test
    fun `GIVEN savedInstanceState is null AND an unknown initial mode argument WHEN restoreFromState is called THEN cameraMode stays at LENS default`() {
        val fragment = LensCameraFragment()
        fragment.arguments = Bundle().apply { putString(LensCameraFragment.ARG_INITIAL_MODE, "UNKNOWN") }

        fragment.restoreFromState(null)

        assertEquals(CameraMode.LENS, fragment.cameraMode.value)
    }

    @Test
    fun `GIVEN savedInstanceState contains LENS AND an initial mode argument of QR WHEN restoreFromState is called THEN the saved mode wins`() {
        val fragment = LensCameraFragment()
        fragment.arguments = Bundle().apply { putString(LensCameraFragment.ARG_INITIAL_MODE, "QR") }
        val savedState = Bundle().apply { putString("camera_mode", "LENS") }

        fragment.restoreFromState(savedState)

        assertEquals(CameraMode.LENS, fragment.cameraMode.value)
    }

    @Test
    fun `GIVEN QR only AND savedInstanceState contains LENS WHEN restoreFromState is called THEN cameraMode is QR`() {
        val fragment = LensCameraFragment()
        fragment.arguments =
            Bundle().apply {
                putBoolean(LensCameraFragment.ARG_QR_ONLY, true)
                putString(LensCameraFragment.ARG_INITIAL_MODE, "LENS")
            }
        val savedState = Bundle().apply { putString("camera_mode", "LENS") }

        fragment.restoreFromState(savedState)

        assertEquals(CameraMode.QR, fragment.cameraMode.value)
    }

    @Test
    fun `GIVEN qrResultSent is true WHEN onSaveInstanceState is called THEN it is written to the bundle`() {
        val fragment = LensCameraFragment()
        fragment.qrResultSent = true
        val outState = Bundle()

        fragment.onSaveInstanceState(outState)

        assertTrue(outState.getBoolean("qr_result_sent"))
    }

    @Test
    fun `GIVEN savedInstanceState contains qrResultSent true WHEN restoreFromState is called THEN qrResultSent is restored`() {
        val fragment = LensCameraFragment()
        val savedState = Bundle().apply { putBoolean("qr_result_sent", true) }

        fragment.restoreFromState(savedState)

        assertTrue(fragment.qrResultSent)
    }

    @Test
    fun `GIVEN savedInstanceState lacks qrResultSent WHEN restoreFromState is called THEN qrResultSent defaults to false`() {
        val fragment = LensCameraFragment()
        val savedState = Bundle()

        fragment.restoreFromState(savedState)

        assertFalse(fragment.qrResultSent)
    }

    // --- buildGalleryRequestBundle tests ---

    @Test
    fun `GIVEN cameraMode is LENS WHEN buildGalleryRequestBundle is called THEN it signals the LENS gallery request`() {
        val fragment = LensCameraFragment()
        fragment.cameraMode.value = CameraMode.LENS

        val bundle = fragment.buildGalleryRequestBundle()

        assertTrue(bundle.getBoolean(LensCameraFragment.RESULT_GALLERY_REQUEST))
        assertFalse(bundle.getBoolean(LensCameraFragment.RESULT_QR_GALLERY_REQUEST))
    }

    @Test
    fun `GIVEN cameraMode is QR WHEN buildGalleryRequestBundle is called THEN it signals the QR gallery request`() {
        val fragment = LensCameraFragment()
        fragment.cameraMode.value = CameraMode.QR

        val bundle = fragment.buildGalleryRequestBundle()

        assertTrue(bundle.getBoolean(LensCameraFragment.RESULT_QR_GALLERY_REQUEST))
        assertFalse(bundle.getBoolean(LensCameraFragment.RESULT_GALLERY_REQUEST))
    }
}
