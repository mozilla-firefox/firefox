/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.qr

import android.Manifest.permission
import android.content.Context
import android.content.pm.PackageManager
import android.graphics.Insets
import android.graphics.Point
import android.graphics.Rect
import android.hardware.camera2.CameraAccessException
import android.hardware.camera2.CameraCaptureSession
import android.hardware.camera2.CameraDevice
import android.hardware.camera2.CameraManager
import android.hardware.camera2.CaptureRequest
import android.hardware.camera2.params.SessionConfiguration
import android.media.Image
import android.media.ImageReader
import android.os.Build
import android.os.Handler
import android.os.HandlerThread
import android.os.Looper.getMainLooper
import android.util.Size
import android.view.Display
import android.view.Surface
import android.view.View
import android.view.WindowInsets
import android.view.WindowManager
import android.view.WindowMetrics
import android.widget.TextView
import androidx.appcompat.widget.AppCompatImageButton
import androidx.core.view.WindowInsetsCompat
import androidx.fragment.app.FragmentActivity
import androidx.test.ext.junit.runners.AndroidJUnit4
import com.google.zxing.BarcodeFormat
import com.google.zxing.BinaryBitmap
import com.google.zxing.LuminanceSource
import com.google.zxing.MultiFormatReader
import com.google.zxing.NotFoundException
import com.google.zxing.PlanarYUVLuminanceSource
import java.nio.ByteBuffer
import java.util.concurrent.ExecutorService
import java.util.concurrent.RejectedExecutionException
import kotlin.test.assertNotNull
import mozilla.components.feature.qr.QrFragment.Companion.chooseOptimalSize
import mozilla.components.feature.qr.views.AutoFitTextureView
import mozilla.components.feature.qr.views.CustomViewFinder
import mozilla.components.support.test.any
import mozilla.components.support.test.argumentCaptor
import mozilla.components.support.test.eq
import mozilla.components.support.test.mock
import mozilla.components.support.test.robolectric.testContext
import mozilla.components.support.test.whenever
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertSame
import org.junit.Assert.assertTrue
import org.junit.Assert.fail
import org.junit.Test
import org.junit.runner.RunWith
import org.mockito.ArgumentMatchers.anyBoolean
import org.mockito.ArgumentMatchers.anyInt
import org.mockito.ArgumentMatchers.anyString
import org.mockito.Mockito.doNothing
import org.mockito.Mockito.doReturn
import org.mockito.Mockito.never
import org.mockito.Mockito.spy
import org.mockito.Mockito.times
import org.mockito.Mockito.verify
import org.mockito.Mockito.`when`
import org.robolectric.Shadows.shadowOf
import org.robolectric.annotation.Config

@RunWith(AndroidJUnit4::class)
class QrFragmentTest {

    @Test
    fun `onPause closes camera, stops background thread, and shuts down executor service`() {
        val scanCompleteListener = mock<QrFragment.OnScanCompleteListener>()
        val qrFragment = spy(QrFragment.newInstance(scanCompleteListener))
        qrFragment.onPause()

        verify(qrFragment).stopBackgroundThread()
        verify(qrFragment).stopExecutorService()
        verify(qrFragment).closeCamera()
    }

    @Test
    fun `onResume opens camera, starts background thread and starts executor service`() {
        val scanCompleteListener = mock<QrFragment.OnScanCompleteListener>()
        val qrFragment = spy(QrFragment.newInstance(scanCompleteListener))
        val context: Context = mock()
        doReturn(PackageManager.PERMISSION_GRANTED)
            .`when`(context)
            .checkPermission(eq(permission.CAMERA), anyInt(), anyInt())
        doReturn(context).`when`(qrFragment).context
        doNothing().`when`(qrFragment).startScanning()

        qrFragment.onResume()

        verify(qrFragment).startScanning()
    }

    @Test
    fun `onResume avoids starting scanning if the camera permission is missing`() {
        val scanCompleteListener = mock<QrFragment.OnScanCompleteListener>()
        val qrFragment = spy(QrFragment.newInstance(scanCompleteListener))
        val context: Context = mock()
        doReturn(PackageManager.PERMISSION_DENIED)
            .`when`(context)
            .checkPermission(eq(permission.CAMERA), anyInt(), anyInt())
        doReturn(context).`when`(qrFragment).context
        doNothing().`when`(qrFragment).startScanning()

        qrFragment.onResume()

        verify(qrFragment, never()).startScanning()
    }

    @Test
    @Config(sdk = [Build.VERSION_CODES.P])
    fun `WHEN calling createCaptureSessionCompat on a device higher than P THEN use newer api`() {
        val scanCompleteListener = mock<QrFragment.OnScanCompleteListener>()
        val qrFragment = spy(QrFragment.newInstance(scanCompleteListener))
        val camera = mock<CameraDevice>()
        val imageSurface = mock<Surface>()
        val surface = mock<Surface>()
        val stateCallback = mock<CameraCaptureSession.StateCallback>()

        doNothing().`when`(qrFragment).maybeStartExecutorService()
        whenever(qrFragment.shouldStartExecutorService()).thenReturn(true)

        qrFragment.backgroundExecutor = mock()
        qrFragment.textureView = mock()
        qrFragment.cameraErrorView = mock()
        qrFragment.customViewFinder = mock()
        whenever(qrFragment.textureView.isAvailable).thenReturn(true)

        qrFragment.createCaptureSessionCompat(camera, imageSurface, surface, stateCallback)

        verify(camera).createCaptureSession(any<SessionConfiguration>())
    }

    @Test
    fun `onStop resets state`() {
        val scanCompleteListener = mock<QrFragment.OnScanCompleteListener>()
        val qrFragment = spy(QrFragment.newInstance(scanCompleteListener))
        QrFragment.qrState = QrFragment.STATE_DECODE_PROGRESS

        qrFragment.onStop()

        assertEquals(QrFragment.STATE_FIND_QRCODE, QrFragment.qrState)
    }

    @Test
    fun `onViewCreated sets initial state`() {
        val scanCompleteListener = mock<QrFragment.OnScanCompleteListener>()
        val qrFragment = spy(QrFragment.newInstance(scanCompleteListener))
        val view: View = mock()
        val textureView: AutoFitTextureView = mock()
        val viewFinder: CustomViewFinder = mock()
        val cameraErrorView: TextView = mock()
        val backButtonView: AppCompatImageButton = mock()

        whenever(view.getContext()).thenReturn(testContext)

        whenever(view.findViewById<AutoFitTextureView>(R.id.texture)).thenReturn(textureView)
        whenever(view.findViewById<CustomViewFinder>(R.id.view_finder)).thenReturn(viewFinder)
        whenever(view.findViewById<TextView>(R.id.camera_error)).thenReturn(cameraErrorView)
        whenever(view.findViewById<AppCompatImageButton>(R.id.back_button)).thenReturn(backButtonView)

        qrFragment.onViewCreated(view, mock())
        assertEquals(QrFragment.STATE_FIND_QRCODE, QrFragment.qrState)
    }

    @Test
    fun `qr fragment has the correct scan message resource`() {
        val listener = mock<QrFragment.OnScanCompleteListener>()

        val qrFragmentWithMessage = QrFragment.newInstance(listener, R.string.mozac_feature_qr_scanner)
        val qrFragmentNoMessage = QrFragment.newInstance(listener, null)

        assertEquals(null, qrFragmentNoMessage.scanMessage)
        assertEquals(R.string.mozac_feature_qr_scanner, qrFragmentWithMessage.scanMessage)
    }

    @Test
    fun `listener is invoked on successful qr scan`() {
        val listener = mock<QrFragment.OnScanCompleteListener>()
        val reader = mock<MultiFormatReader>()
        val qrFragment = spy(QrFragment.newInstance(listener))
        val source = mock<PlanarYUVLuminanceSource>()
        val result = com.google.zxing.Result("qrcode-result", ByteArray(0), emptyArray(), BarcodeFormat.ITF)
        whenever(reader.decodeWithState(any())).thenReturn(result)
        qrFragment.multiFormatReader = reader
        qrFragment.scanCompleteListener = listener
        QrFragment.qrState = QrFragment.STATE_DECODE_PROGRESS

        qrFragment.tryScanningSource(source)
        shadowOf(getMainLooper()).idle()

        verify(listener).onScanComplete(eq("qrcode-result"))
        assertEquals(QrFragment.STATE_QRCODE_EXIST, QrFragment.qrState)
    }

    @Test
    fun `GIVEN the listener is cleared after a qr code is decoded WHEN the result is delivered THEN it is dropped`() {
        val listener = mock<QrFragment.OnScanCompleteListener>()
        val reader = mock<MultiFormatReader>()
        val qrFragment = spy(QrFragment.newInstance(listener))
        val source = mock<PlanarYUVLuminanceSource>()
        val result = com.google.zxing.Result("qrcode-result", ByteArray(0), emptyArray(), BarcodeFormat.ITF)
        whenever(reader.decodeWithState(any())).thenReturn(result)
        qrFragment.multiFormatReader = reader
        QrFragment.qrState = QrFragment.STATE_DECODE_PROGRESS

        qrFragment.tryScanningSource(source)
        qrFragment.scanCompleteListener = null
        shadowOf(getMainLooper()).idle()

        verify(listener, never()).onScanComplete(any())
    }

    @Test
    fun `resets state after each decoding attempt`() {
        val listener = mock<QrFragment.OnScanCompleteListener>()
        val reader = mock<MultiFormatReader>()
        val qrFragment = spy(QrFragment.newInstance(listener))

        val source = mock<PlanarYUVLuminanceSource>()
        val invertedSource = mock<PlanarYUVLuminanceSource>()

        val bitmap = mock<BinaryBitmap>()
        val invertedBitmap = mock<BinaryBitmap>()

        whenever(source.invert()).thenReturn(invertedSource)

        with(qrFragment) {
            whenever(createBinaryBitmap(source)).thenReturn(bitmap)
            whenever(createBinaryBitmap(invertedSource)).thenReturn(invertedBitmap)
        }

        qrFragment.multiFormatReader = reader

        QrFragment.qrState = QrFragment.STATE_DECODE_PROGRESS

        qrFragment.tryScanningSource(source)

        assertEquals(QrFragment.STATE_FIND_QRCODE, QrFragment.qrState)
        verify(reader, times(2)).reset()
    }

    @Test
    fun `don't consider scanning complete if decoding not in progress`() {
        val listener = mock<QrFragment.OnScanCompleteListener>()
        val reader = mock<MultiFormatReader>()
        val qrFragment = spy(QrFragment.newInstance(listener))
        val source = mock<PlanarYUVLuminanceSource>()
        qrFragment.scanCompleteListener = listener
        qrFragment.multiFormatReader = reader
        whenever(reader.decodeWithState(any())).thenThrow(NotFoundException::class.java)
        QrFragment.qrState = QrFragment.STATE_FIND_QRCODE

        qrFragment.tryScanningSource(source)

        verify(reader, never()).decodeWithState(any())
        verify(listener, never()).onScanComplete(any())
    }

    @Test
    fun `early return null for decoding attempt if decoding not in progress`() {
        val listener = mock<QrFragment.OnScanCompleteListener>()
        val reader = mock<MultiFormatReader>()
        val qrFragment = spy(QrFragment.newInstance(listener))
        val source = mock<PlanarYUVLuminanceSource>()
        qrFragment.multiFormatReader = reader
        whenever(reader.decodeWithState(any())).thenThrow(NotFoundException::class.java)
        QrFragment.qrState = QrFragment.STATE_FIND_QRCODE
        qrFragment.tryScanningSource(source)

        verify(qrFragment, never()).decodeSource(any())
        verify(reader, never()).decodeWithState(any())
        verify(listener, never()).onScanComplete(any())
    }

    @Test
    fun `async scanning decodes original unmodified source`() {
        val listener = mock<QrFragment.OnScanCompleteListener>()
        val reader = mock<MultiFormatReader>()
        val qrFragment = spy(QrFragment.newInstance(listener))
        val imageCaptor = argumentCaptor<BinaryBitmap>()
        val source = mock<LuminanceSource>()
        val bitmap = mock<BinaryBitmap>()
        val result = mock<com.google.zxing.Result>()
        qrFragment.multiFormatReader = reader
        QrFragment.qrState = QrFragment.STATE_DECODE_PROGRESS

        with(qrFragment) {
            whenever(createBinaryBitmap(source)).thenReturn(bitmap)
        }

        whenever(reader.decodeWithState(bitmap)).thenReturn(result)

        qrFragment.tryScanningSource(source)
        verify(reader).decodeWithState(imageCaptor.capture())
        assertSame(bitmap, imageCaptor.value)
    }

    @Test
    fun `camera is closed on disconnect and error`() {
        val scanCompleteListener = mock<QrFragment.OnScanCompleteListener>()
        val qrFragment = spy(QrFragment.newInstance(scanCompleteListener))

        var camera: CameraDevice = mock()
        qrFragment.stateCallback.onDisconnected(camera)
        verify(camera).close()

        camera = mock()
        qrFragment.stateCallback.onError(camera, 0)
        verify(camera).close()
    }

    @Test
    fun `catches and handles CameraAccessException when creating preview session`() {
        val scanCompleteListener = mock<QrFragment.OnScanCompleteListener>()
        val qrFragment = spy(QrFragment.newInstance(scanCompleteListener))

        val camera: CameraDevice = mock()
        whenever(camera.createCaptureRequest(anyInt())).thenThrow(CameraAccessException(123))
        qrFragment.cameraDevice = camera

        val textureView: AutoFitTextureView = mock()
        whenever(textureView.surfaceTexture).thenReturn(mock())
        qrFragment.textureView = textureView

        qrFragment.previewSize = mock()

        try {
            qrFragment.createCameraPreviewSession()
        } catch (e: CameraAccessException) {
            fail("CameraAccessException should have been caught and logged, not re-thrown.")
        }
    }

    @Test
    fun `catches and handles IllegalStateException when creating preview session`() {
        val scanCompleteListener = mock<QrFragment.OnScanCompleteListener>()
        val qrFragment = spy(QrFragment.newInstance(scanCompleteListener))

        val camera: CameraDevice = mock()
        whenever(camera.createCaptureRequest(anyInt()))
            .thenThrow(IllegalStateException("CameraDevice was already closed"))
        qrFragment.cameraDevice = camera

        val textureView: AutoFitTextureView = mock()
        whenever(textureView.surfaceTexture).thenReturn(mock())
        qrFragment.textureView = textureView

        qrFragment.previewSize = mock()

        try {
            qrFragment.createCameraPreviewSession()
        } catch (e: IllegalStateException) {
            fail("IllegalStateException should have been caught and logged, not re-thrown.")
        }
    }

    @Test
    fun `catches and handles CameraAccessException when opening camera`() {
        val scanCompleteListener = mock<QrFragment.OnScanCompleteListener>()
        val qrFragment = spy(QrFragment.newInstance(scanCompleteListener))
        whenever(qrFragment.setUpCameraOutputs(anyInt(), anyInt())).then {}

        val cameraManager: CameraManager = mock()
        whenever(cameraManager.openCamera(anyString(), any<CameraDevice.StateCallback>(), any()))
            .thenThrow(CameraAccessException(123))

        val activity: FragmentActivity = mock()
        whenever(activity.getSystemService(Context.CAMERA_SERVICE)).thenReturn(cameraManager)
        whenever(qrFragment.activity).thenReturn(activity)
        qrFragment.cameraId = "mockCamera"

        try {
            qrFragment.openCamera(1920, 1080)
        } catch (e: CameraAccessException) {
            fail("CameraAccessException should have been caught and logged, not re-thrown.")
        }
    }

    @Test
    fun `throws exception on device without camera`() {
        val scanCompleteListener = mock<QrFragment.OnScanCompleteListener>()
        val qrFragment = spy(QrFragment.newInstance(scanCompleteListener))
        whenever(qrFragment.setUpCameraOutputs(anyInt(), anyInt())).then {}

        val cameraManager: CameraManager = mock()
        val activity: FragmentActivity = mock()
        whenever(activity.getSystemService(Context.CAMERA_SERVICE)).thenReturn(cameraManager)
        whenever(qrFragment.activity).thenReturn(activity)

        qrFragment.cameraId = null
        try {
            qrFragment.openCamera(1920, 1080)
            fail("Expected IllegalStateException")
        } catch (e: IllegalStateException) {
            assertEquals("No camera found on device", e.message)
        }
    }

    @Test
    fun `choose optimal size`() {
        var size =
            chooseOptimalSize(
                arrayOf(Size(640, 480), Size(1024, 768)),
                640,
                480,
                QrFragment.MAX_PREVIEW_WIDTH,
                QrFragment.MAX_PREVIEW_HEIGHT,
                Size(16, 9),
            )

        assertEquals(640, size.width)
        assertEquals(480, size.height)

        size =
            chooseOptimalSize(
                arrayOf(Size(1024, 768), Size(640, 480)),
                1024,
                768,
                QrFragment.MAX_PREVIEW_WIDTH,
                QrFragment.MAX_PREVIEW_HEIGHT,
                Size(4, 3),
            )

        assertEquals(640, size.width)
        assertEquals(480, size.height)

        size =
            chooseOptimalSize(
                arrayOf(Size(1024, 768), Size(640, 480), Size(320, 240)),
                2048,
                768,
                QrFragment.MAX_PREVIEW_WIDTH,
                QrFragment.MAX_PREVIEW_HEIGHT,
                Size(4, 3),
            )

        assertEquals(640, size.width)
        assertEquals(480, size.height)

        size =
            chooseOptimalSize(
                arrayOf(Size(1024, 768), Size(640, 480), Size(320, 240)),
                1024,
                1024,
                QrFragment.MAX_PREVIEW_WIDTH,
                QrFragment.MAX_PREVIEW_HEIGHT,
                Size(4, 3),
            )

        assertEquals(640, size.width)
        assertEquals(480, size.height)

        size =
            chooseOptimalSize(
                arrayOf(Size(1024, 768), Size(786, 480), Size(320, 240)),
                2048,
                1024,
                QrFragment.MAX_PREVIEW_WIDTH,
                QrFragment.MAX_PREVIEW_HEIGHT,
                Size(16, 9),
            )

        assertEquals(1024, size.width)
        assertEquals(768, size.height)
    }

    @Test
    fun `read image source adjusts for rowstride`() {
        val image: Image = mock()
        val plane: Image.Plane = mock()

        `when`(image.height).thenReturn(1080)
        `when`(image.width).thenReturn(1920)
        `when`(plane.pixelStride).thenReturn(1)
        `when`(image.planes).thenReturn(arrayOf(plane))

        // Create an image source where rowstride is equal to the width
        val bytesWithEqualRowStride: ByteBuffer = ByteBuffer.allocate(1080 * 1920)
        `when`(plane.buffer).thenReturn(bytesWithEqualRowStride)
        `when`(plane.rowStride).thenReturn(1920)
        assertArrayEquals(bytesWithEqualRowStride.array(), QrFragment.readImageSource(image).matrix)

        // Create an image source where rowstride is greater than the width
        val bytesWithNotEqualRowStride: ByteBuffer = ByteBuffer.allocate(1080 * (1920 + 128))
        `when`(plane.buffer).thenReturn(bytesWithNotEqualRowStride)
        `when`(plane.rowStride).thenReturn(2048)

        // The rowstride / offset should have been taken into account resulting
        // in the same 1080 * 1920 image source as if the rowstride was equal to the width
        assertArrayEquals(bytesWithEqualRowStride.array(), QrFragment.readImageSource(image).matrix)
    }

    @Test
    fun `uses square preview of optimal size`() {
        val scanCompleteListener = mock<QrFragment.OnScanCompleteListener>()
        val qrFragment = spy(QrFragment.newInstance(scanCompleteListener))
        val textureView: AutoFitTextureView = mock()
        qrFragment.textureView = textureView

        var optimalSize =
            chooseOptimalSize(
                arrayOf(Size(640, 480), Size(1024, 768)),
                640,
                480,
                QrFragment.MAX_PREVIEW_WIDTH,
                QrFragment.MAX_PREVIEW_HEIGHT,
                Size(16, 9),
            )
        qrFragment.adjustPreviewSize(optimalSize)
        verify(textureView).setAspectRatio(480, 480)
        assertEquals(480, qrFragment.previewSize?.width)
        assertEquals(480, qrFragment.previewSize?.height)

        optimalSize =
            chooseOptimalSize(
                arrayOf(Size(1024, 768), Size(640, 480), Size(320, 240)),
                2048,
                1024,
                QrFragment.MAX_PREVIEW_WIDTH,
                QrFragment.MAX_PREVIEW_HEIGHT,
                Size(16, 9),
            )
        qrFragment.adjustPreviewSize(optimalSize)
        verify(textureView).setAspectRatio(768, 768)
        assertEquals(768, qrFragment.previewSize?.width)
        assertEquals(768, qrFragment.previewSize?.height)
    }

    @Test
    fun `tryOpenCamera displays error message if no camera is available`() {
        val scanCompleteListener = mock<QrFragment.OnScanCompleteListener>()
        val qrFragment = spy(QrFragment.newInstance(scanCompleteListener))

        qrFragment.textureView = mock()
        qrFragment.cameraErrorView = mock()
        qrFragment.customViewFinder = mock()

        qrFragment.tryOpenCamera(0, 0)
        verify(qrFragment.cameraErrorView).visibility = View.VISIBLE
        verify(qrFragment.customViewFinder).visibility = View.GONE
    }

    @Test
    fun `tryOpenCamera opens camera if available and hides the error message is shown`() {
        val scanCompleteListener = mock<QrFragment.OnScanCompleteListener>()
        val qrFragment = spy(QrFragment.newInstance(scanCompleteListener))
        qrFragment.textureView = mock()
        qrFragment.cameraErrorView = mock()
        qrFragment.customViewFinder = mock()
        doNothing().`when`(qrFragment).openCamera(anyInt(), anyInt())

        qrFragment.tryOpenCamera(0, 0, skipCheck = true)

        verify(qrFragment).openCamera(0, 0)
        verify(qrFragment.cameraErrorView).visibility = View.GONE
        verify(qrFragment.customViewFinder).visibility = View.VISIBLE
    }

    @Test
    fun `tryOpenCamera displays error message if camera throws exception`() {
        val scanCompleteListener = mock<QrFragment.OnScanCompleteListener>()
        val qrFragment = spy(QrFragment.newInstance(scanCompleteListener))
        whenever(qrFragment.setUpCameraOutputs(anyInt(), anyInt())).then {}

        qrFragment.textureView = mock()
        qrFragment.cameraErrorView = mock()
        qrFragment.customViewFinder = mock()

        val cameraManager: CameraManager = mock()
        whenever(cameraManager.openCamera(anyString(), any<CameraDevice.StateCallback>(), any()))
            .thenThrow(IllegalStateException("no camera"))

        val activity: FragmentActivity = mock()
        whenever(activity.getSystemService(Context.CAMERA_SERVICE)).thenReturn(cameraManager)
        whenever(qrFragment.activity).thenReturn(activity)
        qrFragment.cameraId = "mockCamera"

        qrFragment.tryOpenCamera(0, 0, skipCheck = true)
        verify(qrFragment.cameraErrorView).visibility = View.VISIBLE
        verify(qrFragment.customViewFinder).visibility = View.GONE
    }

    @Test
    fun `tries to decode inverted source on original source decode exception`() {
        val listener = mock<QrFragment.OnScanCompleteListener>()
        val reader = mock<MultiFormatReader>()
        val qrFragment = spy(QrFragment.newInstance(listener))
        val imageCaptor = argumentCaptor<BinaryBitmap>()

        val source = mock<LuminanceSource>()
        val invertedSource = mock<LuminanceSource>()
        whenever(source.invert()).thenReturn(invertedSource)

        val bitmap = mock<BinaryBitmap>()
        val invertedBitmap = mock<BinaryBitmap>()

        qrFragment.multiFormatReader = reader
        QrFragment.qrState = QrFragment.STATE_DECODE_PROGRESS

        with(qrFragment) {
            whenever(createBinaryBitmap(source)).thenReturn(bitmap)
            whenever(createBinaryBitmap(invertedSource)).thenReturn(invertedBitmap)
        }

        whenever(reader.decodeWithState(bitmap)).thenThrow(NotFoundException::class.java)

        qrFragment.tryScanningSource(source)

        verify(reader, times(2)).decodeWithState(imageCaptor.capture())
        assertSame(bitmap, imageCaptor.allValues[0])
        assertSame(invertedBitmap, imageCaptor.allValues[1])
    }

    @Test
    fun `tries to decode inverted source when original source returns null`() {
        val listener = mock<QrFragment.OnScanCompleteListener>()
        val reader = mock<MultiFormatReader>()
        val qrFragment = spy(QrFragment.newInstance(listener))
        val imageCaptor = argumentCaptor<BinaryBitmap>()

        val source = mock<LuminanceSource>()
        val invertedSource = mock<LuminanceSource>()
        whenever(source.invert()).thenReturn(invertedSource)

        val bitmap = mock<BinaryBitmap>()
        val invertedBitmap = mock<BinaryBitmap>()

        qrFragment.multiFormatReader = reader
        QrFragment.qrState = QrFragment.STATE_DECODE_PROGRESS

        with(qrFragment) {
            whenever(createBinaryBitmap(source)).thenReturn(bitmap)
            whenever(createBinaryBitmap(invertedSource)).thenReturn(invertedBitmap)
        }

        whenever(reader.decodeWithState(bitmap)).thenReturn(null)

        qrFragment.tryScanningSource(source)

        verify(reader, times(2)).decodeWithState(imageCaptor.capture())
        assertSame(bitmap, imageCaptor.allValues[0])
        assertSame(invertedBitmap, imageCaptor.allValues[1])
    }

    @Test
    @Suppress("DEPRECATION")
    @Config(sdk = [29])
    fun `GIVEN a device rotation of 90 deg WHEN getting the device rotation on a device below SDK 30 THEN the rotation should be 90 deg`() {
        val mockActivity: FragmentActivity = mock()
        val mockManager: WindowManager = mock()
        val mockDisplay: Display = mock()

        val testRotation = Surface.ROTATION_90

        whenever(mockActivity.windowManager).thenReturn(mockManager)
        whenever(mockManager.defaultDisplay).thenReturn(mockDisplay)
        whenever(mockDisplay.rotation).thenReturn(testRotation)

        val listener = mock<QrFragment.OnScanCompleteListener>()
        val qrFragment = spy(QrFragment.newInstance(listener))
        whenever(qrFragment.activity).thenReturn(mockActivity)

        val rotation = qrFragment.getScreenRotation()

        assertEquals(testRotation, rotation)
    }

    @Test
    fun `GIVEN a device rotation of 90 deg WHEN getting the device rotation on a device above SDK 30 THEN the rotation should be 90 deg`() {
        val mockActivity: FragmentActivity = mock()
        val mockDisplay: Display = mock()

        val testRotation = Surface.ROTATION_90
        val listener = mock<QrFragment.OnScanCompleteListener>()

        val qrFragment = spy(QrFragment.newInstance(listener))

        whenever(qrFragment.context).thenReturn(mockActivity)
        whenever(mockActivity.display).thenReturn(mockDisplay)
        whenever(mockDisplay.rotation).thenReturn(testRotation)

        val rotation = qrFragment.getScreenRotation()

        assertEquals(testRotation, rotation)
    }

    @Test
    fun `configureTransform uses getScreenRotation method to get rotation`() {
        val listener = mock<QrFragment.OnScanCompleteListener>()
        val qrFragment = spy(QrFragment.newInstance(listener))
        val textureView: AutoFitTextureView = mock()

        qrFragment.previewSize = Size(4, 4)
        qrFragment.textureView = textureView

        qrFragment.configureTransform(4, 4)

        verify(qrFragment, times(1)).getScreenRotation()
    }

    @Test
    fun `setUpCameraOutputs uses getScreenRotation method to get rotation`() {
        val listener = mock<QrFragment.OnScanCompleteListener>()
        val qrFragment = spy(QrFragment.newInstance(listener))

        qrFragment.setUpCameraOutputs(4, 4)

        verify(qrFragment, times(1)).getScreenRotation()
    }

    @Test
    @Suppress("DEPRECATION")
    @Config(sdk = [29])
    fun `getDisplaySize calls defaultDisplay getSize for SDK below 30`() {
        val mockActivity: FragmentActivity = mock()
        val mockManager: WindowManager = mock()
        val mockDisplay: Display = mock()

        whenever(mockActivity.windowManager).thenReturn(mockManager)
        whenever(mockManager.defaultDisplay).thenReturn(mockDisplay)
        whenever(mockDisplay.getSize(any())).then {}

        mockManager.getDisplaySize()

        verify(mockDisplay, times(1)).getSize(any())
    }

    @Test
    fun `test getDisplaySize for SDK 30 and up`() {
        val mockManager: WindowManager = mock()
        val mockWindowMetrics: WindowMetrics = mock()

        val bounds = Rect(0, 0, 170, 270)
        val insets = Insets.of(10, 10, 10, 10)
        val expectedSize = Point(150, 250)

        val mockWindowInsets: WindowInsets = mock()
        whenever(
                mockWindowInsets.getInsetsIgnoringVisibility(
                    WindowInsetsCompat.Type.navigationBars() or WindowInsetsCompat.Type.displayCutout()
                )
            )
            .thenReturn(insets)

        whenever(mockManager.currentWindowMetrics).thenReturn(mockWindowMetrics)
        whenever(mockWindowMetrics.windowInsets).thenReturn(mockWindowInsets)
        whenever(mockWindowMetrics.bounds).thenReturn(bounds)

        val result = mockManager.getDisplaySize()

        assertEquals(expectedSize, result)
    }

    @Test
    fun `maybeStartBackgroundThread does nothing if the thread already exists`() {
        val qrFragment = QrFragment()
        val existingBackgroundThread =
            HandlerThread("test").apply {
                start() // need the thread to be "alive"
            }
        val existingBackgroundHandler: Handler = mock()
        qrFragment.backgroundThread = existingBackgroundThread
        qrFragment.backgroundHandler = existingBackgroundHandler

        qrFragment.maybeStartBackgroundThread()

        assertSame(existingBackgroundThread, qrFragment.backgroundThread)
        assertSame(existingBackgroundHandler, qrFragment.backgroundHandler)
    }

    @Test
    fun `maybeStartBackgroundThread creates and starts a new background thread and handler if doesn't already exist`() {
        val qrFragment = QrFragment()
        qrFragment.backgroundThread = null
        qrFragment.backgroundHandler = null

        qrFragment.maybeStartBackgroundThread()

        assertNotNull(qrFragment.backgroundThread)
        assertTrue(qrFragment.backgroundThread!!.isAlive)
        assertNotNull(qrFragment.backgroundHandler)
    }

    @Test
    fun `maybeStartExecutorService does nothing if the executor already exists`() {
        val qrFragment = QrFragment()
        val existingExecutorService: ExecutorService = mock()
        qrFragment.backgroundExecutor = existingExecutorService

        qrFragment.maybeStartExecutorService()

        assertSame(existingExecutorService, qrFragment.backgroundExecutor)
    }

    @Test
    fun `maybeStartExecutorService creates a new executor service if doesn't exist already`() {
        val qrFragment = QrFragment()
        qrFragment.backgroundExecutor = null

        qrFragment.maybeStartExecutorService()

        assertNotNull(qrFragment.backgroundExecutor)
    }

    @Test
    fun `startScanning opens camera, starts background thread and starts executor service`() {
        val scanCompleteListener = mock<QrFragment.OnScanCompleteListener>()
        val qrFragment = spy(QrFragment.newInstance(scanCompleteListener))
        whenever(qrFragment.setUpCameraOutputs(anyInt(), anyInt())).then {}
        val context: Context = mock()
        doReturn(PackageManager.PERMISSION_GRANTED)
            .`when`(context)
            .checkPermission(eq(permission.CAMERA), anyInt(), anyInt())
        doReturn(context).`when`(qrFragment).context

        qrFragment.textureView = mock()
        qrFragment.cameraErrorView = mock()
        qrFragment.customViewFinder = mock()
        qrFragment.startScanning()
        verify(qrFragment, never()).tryOpenCamera(anyInt(), anyInt(), anyBoolean())

        whenever(qrFragment.textureView.isAvailable).thenReturn(true)
        qrFragment.cameraId = "mockCamera"
        qrFragment.startScanning()
        verify(qrFragment, times(2)).maybeStartBackgroundThread()
        verify(qrFragment, times(2)).maybeStartExecutorService()
        verify(qrFragment).tryOpenCamera(anyInt(), anyInt(), anyBoolean())
    }

    @Test
    fun `WHEN image reader's surface is null THEN creating preview session should not crash `() {
        val scanCompleteListener = mock<QrFragment.OnScanCompleteListener>()
        val qrFragment = spy(QrFragment.newInstance(scanCompleteListener))

        val camera: CameraDevice = mock()
        qrFragment.cameraDevice = camera

        val imageReader: ImageReader = mock()
        qrFragment.imageReader = imageReader

        whenever(imageReader.surface).thenReturn(null)

        val textureView: AutoFitTextureView = mock()
        whenever(textureView.surfaceTexture).thenReturn(mock())
        qrFragment.textureView = textureView

        qrFragment.previewSize = mock()

        try {
            qrFragment.createCameraPreviewSession()
        } catch (e: NullPointerException) {
            fail("NullPointerException should not not have been thrown.")
        }
    }

    @Test
    fun `GIVEN cameraDevice is null WHEN onSessionConfigured is called THEN the session is closed and setRepeatingRequest is not called`() {
        val qrFragment = QrFragment.newInstance(mock<QrFragment.OnScanCompleteListener>())
        qrFragment.cameraDevice = null

        val session: CameraCaptureSession = mock()
        whenever(session.device).thenReturn(mock())
        val request: CaptureRequest = mock()
        val captureCallback = object : CameraCaptureSession.CaptureCallback() {}

        qrFragment.onSessionConfigured(session, request, captureCallback)

        verify(session).close()
        verify(session, never()).setRepeatingRequest(any(), any(), any())
        assertNull(qrFragment.captureSession)
    }

    @Test
    fun `GIVEN cameraDevice is null and the executor is terminated WHEN onSessionConfigured is called THEN the RejectedExecutionException is caught`() {
        val qrFragment = QrFragment.newInstance(mock<QrFragment.OnScanCompleteListener>())
        qrFragment.cameraDevice = null

        val session: CameraCaptureSession = mock()
        whenever(session.device).thenReturn(mock())
        whenever(session.close()).thenThrow(RejectedExecutionException())
        val request: CaptureRequest = mock()
        val captureCallback = object : CameraCaptureSession.CaptureCallback() {}

        try {
            qrFragment.onSessionConfigured(session, request, captureCallback)
        } catch (e: RejectedExecutionException) {
            fail("RejectedExecutionException should have been caught and logged, not re-thrown.")
        }

        verify(session).close()
        verify(session, never()).setRepeatingRequest(any(), any(), any())
        assertNull(qrFragment.captureSession)
    }

    @Test
    fun `GIVEN the thread is interrupted and the executor is terminated WHEN onSessionConfigured is called THEN the RejectedExecutionException is caught`() {
        val qrFragment = QrFragment.newInstance(mock<QrFragment.OnScanCompleteListener>())
        val camera: CameraDevice = mock()
        qrFragment.cameraDevice = camera

        val session: CameraCaptureSession = mock()
        whenever(session.device).thenReturn(camera)
        whenever(session.close()).thenThrow(RejectedExecutionException())
        val request: CaptureRequest = mock()
        val captureCallback = object : CameraCaptureSession.CaptureCallback() {}

        Thread.currentThread().interrupt()
        try {
            qrFragment.onSessionConfigured(session, request, captureCallback)
        } catch (e: RejectedExecutionException) {
            fail("RejectedExecutionException should have been caught and logged, not re-thrown.")
        } finally {
            assertTrue(Thread.interrupted())
        }

        verify(session).close()
        verify(session, never()).setRepeatingRequest(any(), any(), any())
        assertNull(qrFragment.captureSession)
    }

    @Test
    fun `GIVEN the session belongs to a previous camera WHEN onSessionConfigured is called THEN the session is closed and setRepeatingRequest is not called`() {
        val qrFragment = QrFragment.newInstance(mock<QrFragment.OnScanCompleteListener>())
        qrFragment.cameraDevice = mock()

        val session: CameraCaptureSession = mock()
        whenever(session.device).thenReturn(mock())
        val request: CaptureRequest = mock()
        val captureCallback = object : CameraCaptureSession.CaptureCallback() {}

        qrFragment.onSessionConfigured(session, request, captureCallback)

        verify(session).close()
        verify(session, never()).setRepeatingRequest(any(), any(), any())
        assertNull(qrFragment.captureSession)
    }

    @Test
    fun `GIVEN cameraDevice exists WHEN onSessionConfigured is called THEN setRepeatingRequest is called and captureSession is assigned`() {
        val qrFragment = QrFragment.newInstance(mock<QrFragment.OnScanCompleteListener>())
        val camera: CameraDevice = mock()
        qrFragment.cameraDevice = camera

        val session: CameraCaptureSession = mock()
        whenever(session.device).thenReturn(camera)
        val request: CaptureRequest = mock()
        val captureCallback = object : CameraCaptureSession.CaptureCallback() {}

        qrFragment.onSessionConfigured(session, request, captureCallback)

        assertSame(session, qrFragment.captureSession)
        verify(session).setRepeatingRequest(eq(request), eq(captureCallback), any())
        verify(session, never()).close()
    }

    @Test
    fun `GIVEN setRepeatingRequest throws IllegalArgumentException WHEN onSessionConfigured is called THEN it does not crash`() {
        val qrFragment = QrFragment.newInstance(mock<QrFragment.OnScanCompleteListener>())
        val camera: CameraDevice = mock()
        qrFragment.cameraDevice = camera

        val session: CameraCaptureSession = mock()
        whenever(session.device).thenReturn(camera)
        whenever(session.setRepeatingRequest(any(), any(), any()))
            .thenThrow(IllegalArgumentException("CaptureRequest contains unconfigured Input/Output Surface!"))
        val request: CaptureRequest = mock()
        val captureCallback = object : CameraCaptureSession.CaptureCallback() {}

        try {
            qrFragment.onSessionConfigured(session, request, captureCallback)
        } catch (e: IllegalArgumentException) {
            fail("IllegalArgumentException should have been caught and logged, not re-thrown.")
        }
    }

    @Test
    fun `GIVEN a capture session exists WHEN closeCamera is called THEN the session is closed and cleared`() {
        val qrFragment = QrFragment.newInstance(mock<QrFragment.OnScanCompleteListener>())
        val session: CameraCaptureSession = mock()
        qrFragment.captureSession = session

        qrFragment.closeCamera()

        verify(session).close()
        assertNull(qrFragment.captureSession)
    }
}
