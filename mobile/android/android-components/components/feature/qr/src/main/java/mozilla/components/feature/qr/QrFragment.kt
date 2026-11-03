/*
 * Copyright 2014 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *       http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License. */

// This was originally based on AOSP code, hence not being MPL 2.0.
// See https://github.com/mozilla-mobile/android-components/pull/2532
@file:Suppress("AbsentOrWrongFileLicense")

package mozilla.components.feature.qr

import android.Manifest.permission
import android.annotation.SuppressLint
import android.content.Context
import android.content.res.Configuration
import android.graphics.ImageFormat
import android.graphics.Matrix
import android.graphics.Point
import android.graphics.Rect
import android.graphics.RectF
import android.graphics.SurfaceTexture
import android.hardware.camera2.CameraAccessException
import android.hardware.camera2.CameraCaptureSession
import android.hardware.camera2.CameraCharacteristics
import android.hardware.camera2.CameraDevice
import android.hardware.camera2.CameraManager
import android.hardware.camera2.CameraMetadata
import android.hardware.camera2.CaptureRequest
import android.hardware.camera2.params.OutputConfiguration
import android.hardware.camera2.params.SessionConfiguration
import android.media.Image
import android.media.ImageReader
import android.os.Build
import android.os.Bundle
import android.os.Handler
import android.os.HandlerThread
import android.os.Looper
import android.util.Size
import android.view.LayoutInflater
import android.view.Surface
import android.view.TextureView
import android.view.View
import android.view.ViewGroup
import android.view.WindowManager
import android.widget.TextView
import androidx.annotation.StringRes
import androidx.annotation.VisibleForTesting
import androidx.appcompat.widget.AppCompatImageButton
import androidx.core.content.ContextCompat.getColor
import androidx.core.view.WindowInsetsCompat
import androidx.fragment.app.Fragment
import androidx.fragment.app.FragmentFactory
import androidx.fragment.app.setFragmentResult
import com.google.zxing.BinaryBitmap
import com.google.zxing.LuminanceSource
import com.google.zxing.MultiFormatReader
import com.google.zxing.PlanarYUVLuminanceSource
import com.google.zxing.common.HybridBinarizer
import java.io.Serializable
import java.util.Collections
import java.util.concurrent.Executor
import java.util.concurrent.ExecutorService
import java.util.concurrent.Executors
import java.util.concurrent.RejectedExecutionException
import java.util.concurrent.Semaphore
import java.util.concurrent.TimeUnit
import kotlin.math.max
import kotlin.math.min
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import mozilla.components.feature.qr.views.AutoFitTextureView
import mozilla.components.feature.qr.views.CustomViewFinder
import mozilla.components.support.base.log.logger.Logger
import mozilla.components.support.ktx.android.content.hasCamera
import mozilla.components.support.ktx.android.content.isPermissionGranted
import mozilla.components.support.utils.ext.handleBackEvents

/**
 * A [Fragment] that displays a QR scanner.
 *
 * This class is based on Camera2BasicFragment from:
 *
 * https://github.com/googlesamples/android-Camera2Basic https://github.com/kismkof/camera2basic
 */
@Suppress("LargeClass", "TooManyFunctions")
class QrFragment : Fragment() {
    private val logger = Logger("mozac-qr")

    @VisibleForTesting internal var multiFormatReader = MultiFormatReader()
    private val coroutineScope = CoroutineScope(Dispatchers.Default)

    /** [TextureView.SurfaceTextureListener] handles several lifecycle events on a [TextureView]. */
    private val surfaceTextureListener =
        object : TextureView.SurfaceTextureListener {

            override fun onSurfaceTextureAvailable(texture: SurfaceTexture, width: Int, height: Int) {
                tryOpenCamera(width, height)
            }

            override fun onSurfaceTextureSizeChanged(texture: SurfaceTexture, width: Int, height: Int) {
                configureTransform(width, height)
            }

            @Suppress("EmptyFunctionBlock") override fun onSurfaceTextureUpdated(texture: SurfaceTexture) {}

            override fun onSurfaceTextureDestroyed(texture: SurfaceTexture): Boolean {
                return true
            }
        }

    internal lateinit var textureView: AutoFitTextureView
    internal lateinit var customViewFinder: CustomViewFinder
    internal lateinit var cameraErrorView: TextView
    internal lateinit var backButton: AppCompatImageButton

    @StringRes internal var scanMessage: Int? = null
    internal var cameraId: String? = null
    private var isLowLightBoostSupported: Boolean = false
    @VisibleForTesting internal var captureSession: CameraCaptureSession? = null
    internal var cameraDevice: CameraDevice? = null
    internal var previewSize: Size? = null

    /** Listener invoked when the QR scan completed successfully. */
    interface OnScanCompleteListener : Serializable {
        /** Invoked to provide access to the result of the QR scan. */
        fun onScanComplete(result: String)
    }

    internal var scanCompleteListener: OnScanCompleteListener? = null

    /** [CameraDevice.StateCallback] is called when [CameraDevice] changes its state. */
    internal val stateCallback =
        object : CameraDevice.StateCallback() {

            override fun onOpened(cameraDevice: CameraDevice) {
                cameraOpenCloseLock.release()
                this@QrFragment.cameraDevice = cameraDevice
                createCameraPreviewSession()
            }

            override fun onDisconnected(cameraDevice: CameraDevice) {
                cameraOpenCloseLock.release()
                cameraDevice.close()
                this@QrFragment.cameraDevice = null
            }

            override fun onError(cameraDevice: CameraDevice, error: Int) {
                cameraOpenCloseLock.release()
                cameraDevice.close()
                this@QrFragment.cameraDevice = null
            }
        }

    /** [Surface] used in the camera. */
    private var surface: Surface? = null

    /**
     * An additional thread for running tasks that shouldn't block the UI. A [Handler] for running tasks in the
     * background.
     */
    @VisibleForTesting internal var backgroundThread: HandlerThread? = null

    @VisibleForTesting internal var backgroundHandler: Handler? = null

    @VisibleForTesting internal var backgroundExecutor: ExecutorService? = null

    /** A [Semaphore] to prevent the app from exiting before closing the camera. */
    private val cameraOpenCloseLock = Semaphore(1)

    /** Orientation of the camera sensor */
    private var sensorOrientation: Int = 0

    /** An [ImageReader] that handles still image capture. This is the output file for our picture. */
    @VisibleForTesting internal var imageReader: ImageReader? = null

    private val imageAvailableListener =
        object : ImageReader.OnImageAvailableListener {

            private var image: Image? = null

            override fun onImageAvailable(reader: ImageReader) {
                try {
                    image = reader.acquireNextImage()
                    val availableImage = image
                    if (availableImage != null) {
                        val source = readImageSource(availableImage)
                        if (qrState == STATE_FIND_QRCODE) {
                            qrState = STATE_DECODE_PROGRESS

                            coroutineScope.launch {
                                tryScanningSource(source)
                            }
                        }
                    }
                } finally {
                    image?.close()
                }
            }
        }

    @Suppress("DEPRECATION")
    override fun onDestroyView() {
        super.onDestroyView()

        // Release the keyboard
        requireActivity()
            .window
            .setSoftInputMode(
                WindowManager.LayoutParams.SOFT_INPUT_STATE_UNSPECIFIED or
                    WindowManager.LayoutParams.SOFT_INPUT_ADJUST_RESIZE
            )
    }

    override fun onCreateView(inflater: LayoutInflater, container: ViewGroup?, savedInstanceState: Bundle?): View? {
        // Don't let the keyboard push the UI around
        requireActivity()
            .window
            .setSoftInputMode(
                WindowManager.LayoutParams.SOFT_INPUT_ADJUST_NOTHING or
                    WindowManager.LayoutParams.SOFT_INPUT_STATE_ALWAYS_HIDDEN // <- keep IME closed
            )

        return inflater.inflate(R.layout.fragment_layout, container, false)
    }

    override fun onViewCreated(view: View, savedInstanceState: Bundle?) {
        textureView = view.findViewById<View>(R.id.texture) as AutoFitTextureView
        customViewFinder = view.findViewById<View>(R.id.view_finder) as CustomViewFinder
        cameraErrorView = view.findViewById<View>(R.id.camera_error) as TextView
        backButton = view.findViewById<View>(R.id.back_button) as AppCompatImageButton
        backButton.setOnClickListener {
            stopServices()
            handleResult("")
        }

        CustomViewFinder.setMessage(scanMessage)
        qrState = STATE_FIND_QRCODE

        val root = getView()
        root?.isFocusableInTouchMode = true
        root?.requestFocus()

        view.handleBackEvents {
            stopServices()
            handleResult("")
        }
    }

    override fun onResume() {
        super.onResume()

        // It's possible that the Fragment is resumed to a scanning state
        // while in the meantime the camera permission was removed. Avoid any issues.
        if (requireContext().isPermissionGranted(permission.CAMERA)) {
            startScanning()
        }
    }

    override fun onConfigurationChanged(newConfig: Configuration) {
        super.onConfigurationChanged(newConfig)
        startScanning()
    }

    override fun onPause() {
        stopServices()
        super.onPause()
    }

    override fun onStop() {
        // Ensure we'll continue tracking qr codes when the user returns to the application
        qrState = STATE_FIND_QRCODE

        super.onStop()
    }

    private fun stopServices() {
        closeCamera()
        stopBackgroundThread()
        stopExecutorService()
    }

    internal fun maybeStartBackgroundThread() {
        if (backgroundThread == null) {
            backgroundThread = HandlerThread("CameraBackground")
        }

        backgroundThread?.let {
            if (!it.isAlive) {
                it.start()
                backgroundHandler = Handler(it.looper)
            }
        }
    }

    internal fun stopBackgroundThread() {
        backgroundThread?.quitSafely()
        try {
            backgroundThread?.join()
            backgroundThread = null
            backgroundHandler = null
        } catch (e: InterruptedException) {
            logger.debug("Interrupted while stopping background thread", e)
        }
    }

    internal fun maybeStartExecutorService() {
        if (backgroundExecutor == null) {
            backgroundExecutor = Executors.newSingleThreadExecutor()
        }
    }

    internal fun stopExecutorService() {
        backgroundExecutor?.shutdownNow()
        backgroundExecutor = null
    }

    /**
     * Open the camera and start the qr scanning functionality. Assumes the camera permission is granted for the app. If
     * any issues occur this will fail gracefully and show an error message.
     */
    fun startScanning() {
        maybeStartBackgroundThread()
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.P) {
            maybeStartExecutorService()
        }
        // When the screen is turned off and turned back on, the SurfaceTexture is already
        // available, and "onSurfaceTextureAvailable" will not be called. In that case, we can open
        // a camera and start preview from here (otherwise, we wait until the surface is ready in
        // the SurfaceTextureListener).
        if (textureView.isAvailable) {
            tryOpenCamera(textureView.width, textureView.height)
        } else {
            textureView.surfaceTextureListener = surfaceTextureListener
        }
    }

    /**
     * Sets up member variables related to camera.
     *
     * @param width The width of available size for camera preview
     * @param height The height of available size for camera preview
     */
    internal fun setUpCameraOutputs(width: Int, height: Int) {
        val displayRotation = getScreenRotation()

        val manager = activity?.getSystemService(Context.CAMERA_SERVICE) as CameraManager? ?: return

        for (cameraId in manager.cameraIdList) {
            val characteristics = manager.getCameraCharacteristics(cameraId)

            val facing = characteristics.get(CameraCharacteristics.LENS_FACING)
            if (facing == CameraCharacteristics.LENS_FACING_FRONT) {
                continue
            }

            val map = characteristics.get(CameraCharacteristics.SCALER_STREAM_CONFIGURATION_MAP) ?: continue
            val largest = Collections.max(map.getOutputSizes(ImageFormat.YUV_420_888).asList(), CompareSizesByArea())
            imageReader =
                ImageReader.newInstance(MAX_PREVIEW_WIDTH, MAX_PREVIEW_HEIGHT, ImageFormat.YUV_420_888, 2).apply {
                    setOnImageAvailableListener(imageAvailableListener, backgroundHandler)
                }

            // Find out if we need to swap dimension to get the preview size relative to sensor coordinate.

            sensorOrientation = characteristics.get(CameraCharacteristics.SENSOR_ORIENTATION) as Int

            @Suppress("MagicNumber")
            val swappedDimensions =
                when (displayRotation) {
                    Surface.ROTATION_0,
                    Surface.ROTATION_180 -> sensorOrientation == 90 || sensorOrientation == 270
                    Surface.ROTATION_90,
                    Surface.ROTATION_270 -> sensorOrientation == 0 || sensorOrientation == 180
                    else -> false
                }

            val displaySize = activity?.windowManager?.getDisplaySize() ?: Point()

            var rotatedPreviewWidth = width
            var rotatedPreviewHeight = height
            var maxPreviewWidth = displaySize.x
            var maxPreviewHeight = displaySize.y

            if (swappedDimensions) {
                rotatedPreviewWidth = height
                rotatedPreviewHeight = width
                maxPreviewWidth = displaySize.y
                maxPreviewHeight = displaySize.x
            }

            maxPreviewWidth = min(maxPreviewWidth, MAX_PREVIEW_WIDTH)
            maxPreviewHeight = min(maxPreviewHeight, MAX_PREVIEW_HEIGHT)

            val optimalSize =
                chooseOptimalSize(
                    map.getOutputSizes(SurfaceTexture::class.java),
                    rotatedPreviewWidth,
                    rotatedPreviewHeight,
                    maxPreviewWidth,
                    maxPreviewHeight,
                    largest,
                )

            adjustPreviewSize(optimalSize)
            this.cameraId = cameraId
            this.isLowLightBoostSupported = manager.isLowLightBoostSupported(cameraId)
            return
        }
    }

    internal fun adjustPreviewSize(optimalSize: Size) {
        // We're seeing slow unreliable scans with distorted screens on some devices
        // so we're making the preview and scan area a square of the optimal size
        // to prevent that.
        val length = min(optimalSize.height, optimalSize.width)
        textureView.setAspectRatio(length, length)
        this.previewSize = Size(length, length)
    }

    /**
     * Tries to open the camera and displays an error message in case there's no camera available or we fail to open it.
     * Applications should ideally check for camera availability, but we use this as a fallback in case they don't.
     */
    @Suppress("TooGenericExceptionCaught")
    internal fun tryOpenCamera(width: Int, height: Int, skipCheck: Boolean = false) {
        try {
            if (context?.hasCamera() == true || skipCheck) {
                openCamera(width, height)
                hideNoCameraAvailableError()
            } else {
                showNoCameraAvailableError()
            }
        } catch (e: Exception) {
            showNoCameraAvailableError()
        }
    }

    private fun showNoCameraAvailableError() {
        cameraErrorView.visibility = View.VISIBLE
        customViewFinder.visibility = View.GONE
    }

    private fun hideNoCameraAvailableError() {
        cameraErrorView.visibility = View.GONE
        customViewFinder.visibility = View.VISIBLE
    }

    /** Opens the camera specified by [QrFragment.cameraId]. */
    @SuppressLint("MissingPermission")
    @Suppress("ThrowsCount")
    internal fun openCamera(width: Int, height: Int) {
        try {
            setUpCameraOutputs(width, height)
            if (cameraId == null) {
                throw IllegalStateException("No camera found on device")
            }

            configureTransform(width, height)

            val activity = activity
            val manager = activity?.getSystemService(Context.CAMERA_SERVICE) as CameraManager?

            if (!cameraOpenCloseLock.tryAcquire(CAMERA_CLOSE_LOCK_TIMEOUT_MS, TimeUnit.MILLISECONDS)) {
                throw IllegalStateException("Time out waiting to lock camera opening.")
            }
            manager?.openCamera(cameraId as String, stateCallback, backgroundHandler)
        } catch (e: InterruptedException) {
            throw IllegalStateException("Interrupted while trying to lock camera opening.", e)
        } catch (e: CameraAccessException) {
            logger.error("Failed to open camera", e)
        }
    }

    /** Closes the current [CameraDevice]. */
    internal fun closeCamera() {
        try {
            cameraOpenCloseLock.acquire()
            cameraDevice?.close()
            cameraDevice = null
            imageReader?.close()
            imageReader = null

            surface?.release()
            surface = null

            // captureSession should be closed as a last step in case background executor terminated
            captureSession?.close()
            captureSession = null
        } catch (e: InterruptedException) {
            throw IllegalStateException("Interrupted while trying to lock camera closing.", e)
        } catch (e: RejectedExecutionException) {
            // This exception was found in automated testing
            logger.error("backgroundExecutor terminated", e)
        } finally {
            cameraOpenCloseLock.release()
        }
    }

    /**
     * Configures the necessary [android.graphics.Matrix] transformation to `textureView`. This method should be called
     * after the camera preview size is determined in [setUpCameraOutputs] and also the size of `textureView` is fixed.
     *
     * @param viewWidth The width of `textureView`
     * @param viewHeight The height of `textureView`
     */
    @VisibleForTesting
    internal fun configureTransform(viewWidth: Int, viewHeight: Int) {
        val size = previewSize ?: return

        val rotation = getScreenRotation()
        val matrix = Matrix()
        val viewRect = RectF(0f, 0f, viewWidth.toFloat(), viewHeight.toFloat())
        val bufferRect = RectF(0f, 0f, size.height.toFloat(), size.width.toFloat())
        val centerX = viewRect.centerX()
        val centerY = viewRect.centerY()

        @Suppress("MagicNumber")
        if (Surface.ROTATION_90 == rotation || Surface.ROTATION_270 == rotation) {
            bufferRect.offset(centerX - bufferRect.centerX(), centerY - bufferRect.centerY())
            matrix.setRectToRect(viewRect, bufferRect, Matrix.ScaleToFit.FILL)
            val scale = max(viewHeight.toFloat() / size.height, viewWidth.toFloat() / size.width)
            matrix.postScale(scale, scale, centerX, centerY)
            matrix.postRotate((90 * (rotation - 2)).toFloat(), centerX, centerY)
        } else if (Surface.ROTATION_180 == rotation) {
            matrix.postRotate(180f, centerX, centerY)
        }
        textureView.setTransform(matrix)
    }

    /** Creates a new [CameraCaptureSession] for camera preview. */
    internal fun createCameraPreviewSession() {
        val texture = textureView.surfaceTexture

        val size = previewSize as Size
        // We configure the size of default buffer to be the size of camera preview we want.
        texture?.setDefaultBufferSize(size.width, size.height)

        val previewSurface = Surface(texture).also { surface = it }

        // If image reader's surface is null, stop here.
        val imageSurface = imageReader?.surface ?: return

        handleCaptureException("Failed to create camera preview session") {
            cameraDevice?.let {
                val previewRequestBuilder =
                    it.createCaptureRequest(CameraDevice.TEMPLATE_PREVIEW).apply {
                        addTarget(imageSurface)
                        addTarget(previewSurface)
                    }

                previewRequestBuilder.set(
                    CaptureRequest.CONTROL_AF_MODE,
                    CaptureRequest.CONTROL_AF_MODE_CONTINUOUS_PICTURE,
                )

                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.VANILLA_ICE_CREAM && isLowLightBoostSupported) {
                    previewRequestBuilder.set(
                        CaptureRequest.CONTROL_AE_MODE,
                        CameraMetadata.CONTROL_AE_MODE_ON_LOW_LIGHT_BOOST_BRIGHTNESS_PRIORITY,
                    )
                }

                val previewRequest = previewRequestBuilder.build()
                val captureCallback = object : CameraCaptureSession.CaptureCallback() {}
                val stateCallback =
                    object : CameraCaptureSession.StateCallback() {
                        override fun onConfigured(cameraCaptureSession: CameraCaptureSession) {
                            onSessionConfigured(cameraCaptureSession, previewRequest, captureCallback)
                        }

                        override fun onConfigureFailed(cameraCaptureSession: CameraCaptureSession) {
                            logger.error("Failed to configure CameraCaptureSession")
                        }
                    }
                createCaptureSessionCompat(it, imageSurface, previewSurface, stateCallback)
            }
        }
    }

    @VisibleForTesting
    internal fun onSessionConfigured(
        session: CameraCaptureSession,
        previewRequest: CaptureRequest,
        captureCallback: CameraCaptureSession.CaptureCallback,
    ) {
        // Serialize with closeCamera() so a session is never stored for a closed camera. The wait is
        // bounded because before API 28 this runs on backgroundThread, which also has to deliver
        // the onOpened that releases a lock held by a concurrent openCamera().
        val acquired =
            try {
                cameraOpenCloseLock.tryAcquire(CAMERA_CLOSE_LOCK_TIMEOUT_MS, TimeUnit.MILLISECONDS)
            } catch (e: InterruptedException) {
                Thread.currentThread().interrupt()
                false
            }
        if (!acquired) {
            closeUnusedSession(session)
            return
        }
        try {
            if (cameraDevice == null || session.device != cameraDevice) {
                closeUnusedSession(session)
                return
            }
            captureSession = session
        } finally {
            cameraOpenCloseLock.release()
        }

        // closeCamera() can still release the surfaces before this request is submitted.
        handleCaptureException("Failed to request capture") {
            try {
                session.setRepeatingRequest(previewRequest, captureCallback, backgroundHandler)
            } catch (e: IllegalArgumentException) {
                logger.error("Failed to request capture", e)
            }
        }
    }

    private fun closeUnusedSession(session: CameraCaptureSession) {
        try {
            session.close()
        } catch (e: RejectedExecutionException) {
            logger.error("backgroundExecutor terminated", e)
        }
    }

    @VisibleForTesting
    internal fun createCaptureSessionCompat(
        camera: CameraDevice,
        imageSurface: Surface,
        surface: Surface,
        stateCallback: CameraCaptureSession.StateCallback,
    ) {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.P) {
            if (shouldStartExecutorService()) {
                maybeStartExecutorService()
            }
            val sessionConfig =
                SessionConfiguration(
                    SessionConfiguration.SESSION_REGULAR,
                    listOf(OutputConfiguration(imageSurface), OutputConfiguration(surface)),
                    backgroundExecutor as Executor,
                    stateCallback,
                )
            camera.createCaptureSession(sessionConfig)
        } else {
            @Suppress("DEPRECATION") camera.createCaptureSession(listOf(imageSurface, surface), stateCallback, null)
        }
    }

    @VisibleForTesting internal fun shouldStartExecutorService(): Boolean = backgroundExecutor == null

    @Suppress("TooGenericExceptionCaught")
    private fun handleCaptureException(msg: String, block: () -> Unit) {
        try {
            block()
        } catch (e: Exception) {
            when (e) {
                is CameraAccessException,
                is IllegalStateException -> {
                    logger.error(msg, e)
                }
                else -> throw e
            }
        }
    }

    /** Compares two `Size`s based on their areas. */
    internal class CompareSizesByArea : Comparator<Size> {
        override fun compare(lhs: Size, rhs: Size): Int {
            return java.lang.Long.signum(lhs.width.toLong() * lhs.height - rhs.width.toLong() * rhs.height)
        }
    }

    companion object {
        internal const val STATE_FIND_QRCODE = 0
        internal const val STATE_DECODE_PROGRESS = 1
        internal const val STATE_QRCODE_EXIST = 2

        internal const val MAX_PREVIEW_WIDTH = 786
        internal const val MAX_PREVIEW_HEIGHT = 786

        private const val CAMERA_CLOSE_LOCK_TIMEOUT_MS = 2500L

        const val RESULT_REQUEST_KEY = "qr_fragment_result_key"
        const val RESULT_BUNDLE_KEY = "qr_scan_result_bundle"

        /**
         * Returns a new instance of QR Fragment
         *
         * @param listener Listener invoked when the QR scan completed successfully.
         * @param scanMessage (Optional) Scan message to be displayed.
         */
        fun newInstance(listener: OnScanCompleteListener, scanMessage: Int? = null): QrFragment {
            return QrFragment().apply {
                scanCompleteListener = listener
                this.scanMessage = scanMessage
            }
        }

        /**
         * Returns a new instance of QR Fragment. Recommend using the [QrFragmentFactory] to create a [QrFragment].
         *
         * @param scanMessage (Optional) Scan message to be displayed.
         */
        @VisibleForTesting
        internal fun newInstance(scanMessage: Int? = null): QrFragment {
            return QrFragment().apply {
                this.scanMessage = scanMessage
            }
        }

        /**
         * Given `choices` of `Size`s supported by a camera, choose the smallest one that is at least as large as the
         * respective texture view size, and that is at most as large as the respective max size, and whose aspect ratio
         * matches with the specified value. If such size doesn't exist, choose the largest one that is at most as large
         * as the respective max size, and whose aspect ratio matches with the specified value.
         *
         * @param choices The list of sizes that the camera supports for the intended output class
         * @param textureViewWidth The width of the texture view relative to sensor coordinate
         * @param textureViewHeight The height of the texture view relative to sensor coordinate
         * @param maxWidth The maximum width that can be chosen
         * @param maxHeight The maximum height that can be chosen
         * @param aspectRatio The aspect ratio
         * @return The optimal `Size`, or an arbitrary one if none were big enough.
         */
        internal fun chooseOptimalSize(
            choices: Array<Size>,
            textureViewWidth: Int,
            textureViewHeight: Int,
            maxWidth: Int,
            maxHeight: Int,
            aspectRatio: Size,
        ): Size {
            // Collect the supported resolutions that are at least as big as the preview Surface
            val bigEnough = ArrayList<Size>()
            // Collect the supported resolutions that are smaller than the preview Surface
            val notBigEnough = ArrayList<Size>()
            val w = aspectRatio.width
            val h = aspectRatio.height
            for (option in choices) {
                if (option.width <= maxWidth && option.height <= maxHeight && option.height == option.width * h / w) {
                    if (option.width >= textureViewWidth && option.height >= textureViewHeight) {
                        bigEnough.add(option)
                    } else {
                        notBigEnough.add(option)
                    }
                }
            }

            // Pick the smallest of those big enough. If there is no one big enough, pick the
            // largest of those not big enough.
            return when {
                bigEnough.size > 0 -> Collections.min(bigEnough, CompareSizesByArea())
                notBigEnough.size > 0 -> Collections.max(notBigEnough, CompareSizesByArea())
                else -> choices[0]
            }
        }

        internal fun readImageSource(image: Image): PlanarYUVLuminanceSource {
            val plane = image.planes[0]
            val buffer = plane.buffer
            val data = ByteArray(buffer.remaining()).also { buffer.get(it) }

            val height = image.height
            val width = image.width
            val dataWidth = width + ((plane.rowStride - plane.pixelStride * width) / plane.pixelStride)
            return PlanarYUVLuminanceSource(data, dataWidth, height, 0, 0, width, height, false)
        }

        @Volatile internal var qrState: Int = 0
    }

    /** Send result of QR Scan to appropriate listener. */
    internal fun handleResult(result: String?) {
        val resultBundle =
            Bundle().apply {
                putString(RESULT_BUNDLE_KEY, result)
            }

        Handler(Looper.getMainLooper()).post {
            scanCompleteListener?.let { listener ->
                context?.let {
                    customViewFinder.setViewFinderColor(getColor(it, R.color.mozac_feature_qr_scan_success_color))
                }
                listener.onScanComplete(result ?: "")
            }
        }

        if (isAdded) {
            setFragmentResult(RESULT_REQUEST_KEY, resultBundle)
        }
    }

    @VisibleForTesting
    internal fun tryScanningSource(source: LuminanceSource) {
        if (qrState != STATE_DECODE_PROGRESS) {
            return
        }
        val result = decodeSource(source) ?: decodeSource(source.invert())
        result?.let {
            handleResult(result)
        }
    }

    @VisibleForTesting
    @Suppress("TooGenericExceptionCaught")
    internal fun decodeSource(source: LuminanceSource): String? {
        return try {
            val bitmap = createBinaryBitmap(source)
            val rawResult = multiFormatReader.decodeWithState(bitmap)
            if (rawResult != null) {
                qrState = STATE_QRCODE_EXIST
                rawResult.toString()
            } else {
                qrState = STATE_FIND_QRCODE
                null
            }
        } catch (e: Exception) {
            qrState = STATE_FIND_QRCODE
            null
        } finally {
            multiFormatReader.reset()
        }
    }

    @VisibleForTesting internal fun createBinaryBitmap(source: LuminanceSource) = BinaryBitmap(HybridBinarizer(source))

    /**
     * Returns the screen rotation
     *
     * @return the actual rotation of the device is one of these values: [Surface.ROTATION_0], [Surface.ROTATION_90],
     *   [Surface.ROTATION_180], [Surface.ROTATION_270]
     */
    @VisibleForTesting
    internal fun getScreenRotation(): Int? {
        return if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            this.context?.display?.rotation
        } else {
            @Suppress("DEPRECATION") activity?.windowManager?.defaultDisplay?.rotation
        }
    }
}

/** Returns the size of the display, in pixels. */
@VisibleForTesting
internal fun WindowManager.getDisplaySize(): Point {
    val size = Point()
    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
        // Tests for this branch will be added after
        // https://github.com/mozilla-mobile/android-components/issues/9684 is implemented.
        val windowMetrics = this.currentWindowMetrics
        val windowInsets: WindowInsetsCompat = WindowInsetsCompat.toWindowInsetsCompat(windowMetrics.windowInsets)

        val insets =
            windowInsets.getInsetsIgnoringVisibility(
                WindowInsetsCompat.Type.navigationBars() or WindowInsetsCompat.Type.displayCutout()
            )
        val insetsWidth = insets.right + insets.left
        val insetsHeight = insets.top + insets.bottom

        val bounds: Rect = windowMetrics.bounds
        size.set(bounds.width() - insetsWidth, bounds.height() - insetsHeight)
    } else {
        @Suppress("DEPRECATION") this.defaultDisplay.getSize(size)
    }
    return size
}

/**
 * [FragmentFactory] for [QrFragment].
 *
 * @property scanMessageProvider Provider for the scan message.
 */
class QrFragmentFactory(private val scanMessageProvider: () -> Int?) : FragmentFactory() {
    override fun instantiate(classLoader: ClassLoader, className: String): Fragment {
        return when (className) {
            QrFragment::class.java.name -> {
                val scanMessage = scanMessageProvider()
                QrFragment.newInstance(scanMessage)
            }
            else -> super.instantiate(classLoader, className)
        }
    }
}
