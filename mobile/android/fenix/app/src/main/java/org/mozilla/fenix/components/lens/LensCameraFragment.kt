/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.components.lens

import android.annotation.SuppressLint
import android.content.Context
import android.graphics.ImageFormat
import android.graphics.Point
import android.graphics.Rect
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
import android.media.ImageReader
import android.net.Uri
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
import androidx.annotation.VisibleForTesting
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.core.content.FileProvider
import androidx.core.view.WindowInsetsCompat
import androidx.fragment.app.Fragment
import androidx.fragment.app.setFragmentResult
import androidx.fragment.compose.content
import java.io.File
import java.io.IOException
import java.util.Collections
import java.util.concurrent.ExecutorService
import java.util.concurrent.Executors
import java.util.concurrent.RejectedExecutionException
import java.util.concurrent.Semaphore
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicBoolean
import kotlin.math.abs
import kotlinx.coroutines.flow.MutableStateFlow
import mozilla.components.feature.qr.QrAnalyzer
import mozilla.components.feature.qr.isLowLightBoostSupported
import mozilla.components.support.base.log.logger.Logger
import mozilla.components.support.ktx.android.content.hasCamera
import mozilla.components.support.utils.DefaultDateTimeProvider
import mozilla.components.support.utils.ext.handleBackEvents

/**
 * A [Fragment] that displays a camera preview with shutter and gallery buttons for capturing images for Google Lens.
 *
 * @param now A function that returns the current time in milliseconds.
 */
@Suppress("LargeClass", "TooManyFunctions")
class LensCameraFragment(private val now: () -> Long = DefaultDateTimeProvider()::currentTimeMillis) : Fragment() {
    private val logger = Logger("LensCameraFragment")

    @VisibleForTesting internal var textureView: TextureView? = null

    @VisibleForTesting internal val showCameraError = mutableStateOf(false)

    @VisibleForTesting internal var cameraId: String? = null
    private var isLowLightBoostSupported: Boolean = false

    @VisibleForTesting internal var captureSession: CameraCaptureSession? = null

    @VisibleForTesting internal var cameraDevice: CameraDevice? = null

    @VisibleForTesting internal var previewSize: Size? = null

    @VisibleForTesting internal val previewAspectRatio = mutableStateOf<Float?>(null)
    private var sensorOrientation: Int = 0

    @VisibleForTesting internal var surface: Surface? = null

    @VisibleForTesting internal var backgroundThread: HandlerThread? = null

    @VisibleForTesting internal var backgroundHandler: Handler? = null

    @VisibleForTesting internal var backgroundExecutor: ExecutorService? = null

    @VisibleForTesting internal var imageReader: ImageReader? = null

    @VisibleForTesting internal var qrImageReader: ImageReader? = null

    @VisibleForTesting internal var qrAnalyzer = QrAnalyzer()

    // Source of truth for the current camera mode. MutableStateFlow rather than mutableStateOf
    // because qrImageAvailableListener reads .value on the camera background thread.
    @VisibleForTesting internal val cameraMode = MutableStateFlow(CameraMode.LENS)

    @VisibleForTesting internal val qrInFlight = AtomicBoolean(false)

    private val isQrOnly: Boolean
        get() = arguments?.getBoolean(ARG_QR_ONLY, false) == true

    @VisibleForTesting internal var qrResultSent = false

    @VisibleForTesting
    internal var getUriForFile: (Context, String, File) -> Uri = { ctx, authority, file ->
        FileProvider.getUriForFile(ctx, authority, file)
    }
    private var isCapturing = false
    private val cameraOpenCloseLock = Semaphore(1)
    private val mainHandler = Handler(Looper.getMainLooper())

    @VisibleForTesting
    internal val surfaceTextureListener =
        object : TextureView.SurfaceTextureListener {
            override fun onSurfaceTextureAvailable(texture: SurfaceTexture, width: Int, height: Int) {
                tryOpenCamera(width, height)
            }

            override fun onSurfaceTextureSizeChanged(texture: SurfaceTexture, width: Int, height: Int) {
                // TextureView resets the default buffer size to its own pixel size before calling this, which
                // would leave a later OutputConfiguration reading the view size instead of the configured stream
                // size. The camera resize here is driven by previewAspectRatio, so restore what was selected.
                previewSize?.let { texture.setDefaultBufferSize(it.width, it.height) }
                configureTransform(width, height)
            }

            @Suppress("EmptyFunctionBlock") override fun onSurfaceTextureUpdated(texture: SurfaceTexture) {}

            override fun onSurfaceTextureDestroyed(texture: SurfaceTexture): Boolean = true
        }

    @VisibleForTesting
    internal val stateCallback =
        object : CameraDevice.StateCallback() {
            override fun onOpened(camera: CameraDevice) {
                cameraOpenCloseLock.release()
                cameraDevice = camera
                createCameraPreviewSession()
            }

            override fun onDisconnected(camera: CameraDevice) {
                cameraOpenCloseLock.release()
                camera.close()
                cameraDevice = null
            }

            override fun onError(camera: CameraDevice, error: Int) {
                cameraOpenCloseLock.release()
                camera.close()
                cameraDevice = null
                handleResult(null)
            }
        }

    private val stillCaptureCallback =
        object : CameraCaptureSession.CaptureCallback() {
            override fun onCaptureFailed(
                session: CameraCaptureSession,
                request: CaptureRequest,
                failure: android.hardware.camera2.CaptureFailure,
            ) {
                logger.error("Still capture failed with reason: ${failure.reason}")
                mainHandler.post {
                    isCapturing = false
                    handleResult(null)
                }
            }
        }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        restoreFromState(savedInstanceState)
    }

    @VisibleForTesting
    internal fun restoreFromState(savedInstanceState: Bundle?) {
        if (isQrOnly) {
            cameraMode.value = CameraMode.QR
        } else {
            val modeName = savedInstanceState?.getString(STATE_CAMERA_MODE) ?: arguments?.getString(ARG_INITIAL_MODE)
            CameraMode.fromNameOrNull(modeName)?.let { cameraMode.value = it }
        }
        savedInstanceState?.let { state ->
            qrResultSent = state.getBoolean(STATE_QR_RESULT_SENT, false)
        }
    }

    override fun onSaveInstanceState(outState: Bundle) {
        super.onSaveInstanceState(outState)
        outState.putString(STATE_CAMERA_MODE, cameraMode.value.name)
        outState.putBoolean(STATE_QR_RESULT_SENT, qrResultSent)
    }

    override fun onCreateView(inflater: LayoutInflater, container: ViewGroup?, savedInstanceState: Bundle?): View =
        content {
            val mode by cameraMode.collectAsState()
            LensCameraScreen(
                state =
                    LensCameraState(
                        showError = showCameraError.value,
                        mode = mode,
                        previewAspectRatio = previewAspectRatio.value,
                        showModeToggle = !isQrOnly,
                    ),
                onModeChange = ::handleModeChanged,
                onClose = { handleResult(null) },
                onShutter = { captureStillImage() },
                onGallery = { requestGalleryPick() },
                textureViewProvider = { ctx ->
                    TextureView(ctx).also { view ->
                        textureView = view
                        if (isResumed) {
                            startCamera()
                        }
                    }
                },
            )
        }

    override fun onViewCreated(view: View, savedInstanceState: Bundle?) {
        view.isFocusableInTouchMode = true
        view.requestFocus()
        view.handleBackEvents { handleResult(null) }
    }

    override fun onResume() {
        super.onResume()
        qrAnalyzer.reset()
        // qrResultSent is intentionally not reset here. After a successful scan it stays true
        // through process death + restore, so a re-entry to the activity before it finishes
        // can't fire a duplicate result. handleModeChanged(QR) clears it on a fresh mode entry.
        startCamera()
    }

    override fun onPause() {
        stopServices()
        super.onPause()
    }

    @VisibleForTesting
    internal fun startCamera() {
        val view = textureView ?: return
        maybeStartBackgroundThread()
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.P) {
            maybeStartExecutorService()
        }
        if (view.isAvailable) {
            tryOpenCamera(view.width, view.height)
        } else {
            view.surfaceTextureListener = surfaceTextureListener
        }
    }

    @VisibleForTesting
    internal fun stopServices() {
        closeCamera()
        stopBackgroundThread()
        stopExecutorService()
    }

    @Suppress("TooGenericExceptionCaught")
    @VisibleForTesting
    internal fun tryOpenCamera(width: Int, height: Int) {
        try {
            if (deviceHasCamera()) {
                openCamera(width, height)
                showCameraError.value = false
            } else {
                showCameraError.value = true
            }
        } catch (e: Exception) {
            showCameraError.value = true
        }
    }

    @VisibleForTesting internal fun deviceHasCamera(): Boolean = context?.hasCamera() == true

    @SuppressLint("MissingPermission")
    @Suppress("ThrowsCount")
    @VisibleForTesting
    internal fun openCamera(width: Int, height: Int) {
        try {
            setUpCameraOutputs(width, height)
            val id = cameraId ?: throw IllegalStateException("No camera found on device")
            configureTransform(width, height)

            val manager = activity?.getSystemService(Context.CAMERA_SERVICE) as CameraManager?
            if (!cameraOpenCloseLock.tryAcquire(CAMERA_CLOSE_LOCK_TIMEOUT_MS, TimeUnit.MILLISECONDS)) {
                throw IllegalStateException("Time out waiting to lock camera opening.")
            }
            manager?.openCamera(id, stateCallback, backgroundHandler)
        } catch (e: InterruptedException) {
            throw IllegalStateException("Interrupted while trying to lock camera opening.", e)
        } catch (e: CameraAccessException) {
            logger.error("Failed to open camera", e)
        }
    }

    @VisibleForTesting
    internal fun setUpCameraOutputs(width: Int, height: Int) {
        val displayRotation = getScreenRotation() ?: Surface.ROTATION_0
        val manager = activity?.getSystemService(Context.CAMERA_SERVICE) as? CameraManager? ?: return

        for (id in manager.cameraIdList) {
            val characteristics = manager.getCameraCharacteristics(id)
            val facing = characteristics.get(CameraCharacteristics.LENS_FACING)
            if (facing == CameraCharacteristics.LENS_FACING_FRONT) continue

            val map = characteristics.get(CameraCharacteristics.SCALER_STREAM_CONFIGURATION_MAP) ?: continue

            // The three surfaces below are sized together, not independently, so that they land on a single row of
            // camera2's guaranteed stream combinations: PRIV at PREVIEW + YUV at PREVIEW + JPEG at MAXIMUM, which is
            // guaranteed from the LEGACY hardware level up. PREVIEW is min(display size, 1920x1080), enforced for the
            // preview and QR streams by lensPreviewConstraints below; MAXIMUM is the largest JPEG size. Any other mix
            // works only by the grace of the individual device.
            val captureSize = chooseCaptureSize(map.getOutputSizes(ImageFormat.JPEG))

            imageReader =
                ImageReader.newInstance(
                        captureSize.width,
                        captureSize.height,
                        ImageFormat.JPEG,
                        1,
                    )
                    .apply {
                        setOnImageAvailableListener(onImageAvailableListener, backgroundHandler)
                    }

            sensorOrientation = characteristics.get(CameraCharacteristics.SENSOR_ORIENTATION) as Int

            val swappedDimensions = LensPreviewTransform.areDimensionsSwapped(displayRotation, sensorOrientation)
            val displaySize = activity?.windowManager?.let { getDisplaySize(it) } ?: Point()
            val lensPreviewConstraints =
                LensPreviewTransform.previewConstraints(
                    viewWidth = width,
                    viewHeight = height,
                    displaySize = displaySize,
                    swapped = swappedDimensions,
                )

            val optimalSize =
                chooseOptimalSize(
                    map.getOutputSizes(SurfaceTexture::class.java),
                    lensPreviewConstraints.rotatedWidth,
                    lensPreviewConstraints.rotatedHeight,
                    lensPreviewConstraints.maxWidth,
                    lensPreviewConstraints.maxHeight,
                    captureSize,
                )

            val qrSize =
                chooseQrSize(
                    map.getOutputSizes(ImageFormat.YUV_420_888),
                    lensPreviewConstraints.maxWidth,
                    lensPreviewConstraints.maxHeight,
                    optimalSize,
                )

            qrImageReader =
                ImageReader.newInstance(
                        qrSize.width,
                        qrSize.height,
                        ImageFormat.YUV_420_888,
                        QrAnalyzer.YUV_MAX_IMAGES,
                    )
                    .apply {
                        setOnImageAvailableListener(qrImageAvailableListener, backgroundHandler)
                    }

            previewSize = optimalSize
            previewAspectRatio.value = LensPreviewTransform.displayAspectRatio(optimalSize, swappedDimensions)
            this.cameraId = id
            this.isLowLightBoostSupported = manager.isLowLightBoostSupported(id)
            return
        }
    }

    @VisibleForTesting
    internal fun createCameraPreviewSession() {
        val texture = textureView?.surfaceTexture
        val size = previewSize ?: return
        texture?.setDefaultBufferSize(size.width, size.height)

        val previewSurface = Surface(texture).also { surface = it }
        val imageSurface = imageReader?.surface ?: return
        val qrSurface = qrImageReader?.surface ?: return

        handleCaptureException("Failed to create camera preview session") {
            cameraDevice?.let { camera ->
                val previewRequest = buildPreviewRequest(camera, previewSurface, qrSurface)
                val captureCallback = object : CameraCaptureSession.CaptureCallback() {}
                val sessionStateCallback =
                    object : CameraCaptureSession.StateCallback() {
                        override fun onConfigured(session: CameraCaptureSession) {
                            onSessionConfigured(session, previewRequest, captureCallback)
                        }

                        override fun onConfigureFailed(session: CameraCaptureSession) {
                            onSessionConfigureFailed()
                        }
                    }
                createCaptureSessionCompat(camera, imageSurface, qrSurface, previewSurface, sessionStateCallback)
            }
        }
    }

    /** Shows the camera error state. The repeating request never starts, so the preview stays black. */
    @VisibleForTesting
    internal fun onSessionConfigureFailed() {
        logger.error(
            "Failed to configure CameraCaptureSession: preview=$previewSize" +
                ", jpeg=${imageReader?.width}x${imageReader?.height}" +
                ", yuv=${qrImageReader?.width}x${qrImageReader?.height}"
        )
        mainHandler.post { showCameraError.value = true }
    }

    /**
     * Builds the repeating preview request for the active [cameraMode].
     *
     * [qrSurface] stays in the session's output list in both modes so a mode toggle is only a
     * [CameraCaptureSession.setRepeatingRequest] and never a session teardown, but it is only a request target in
     * [CameraMode.QR]. In [CameraMode.LENS] its frames are pure overhead on the camera pipeline.
     */
    @VisibleForTesting
    internal fun buildPreviewRequest(
        camera: CameraDevice,
        previewSurface: Surface,
        qrSurface: Surface,
    ): CaptureRequest {
        val previewRequestBuilder =
            camera.createCaptureRequest(CameraDevice.TEMPLATE_PREVIEW).apply {
                previewTargets(previewSurface, qrSurface).forEach(::addTarget)
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

        return previewRequestBuilder.build()
    }

    /** Repeating-request targets for the active [cameraMode]. */
    @VisibleForTesting
    internal fun previewTargets(previewSurface: Surface, qrSurface: Surface): List<Surface> =
        if (cameraMode.value == CameraMode.QR) {
            listOf(previewSurface, qrSurface)
        } else {
            listOf(previewSurface)
        }

    /** Swaps the repeating request for one matching the active [cameraMode], leaving the session intact. */
    @VisibleForTesting
    internal fun updatePreviewRequest() {
        val session = captureSession ?: return
        val camera = cameraDevice ?: return
        val previewSurface = surface ?: return
        val qrSurface = qrImageReader?.surface ?: return

        handleCaptureException("Failed to update preview request") {
            try {
                session.setRepeatingRequest(
                    buildPreviewRequest(camera, previewSurface, qrSurface),
                    null,
                    backgroundHandler,
                )
            } catch (e: IllegalArgumentException) {
                logger.error("Failed to update preview request", e)
            }
        }
    }

    @VisibleForTesting
    internal fun onSessionConfigured(
        session: CameraCaptureSession,
        previewRequest: CaptureRequest,
        captureCallback: CameraCaptureSession.CaptureCallback,
    ) {
        // closeCamera() runs on the main thread while this callback runs on the background
        // thread. Gate the check-and-assign on cameraOpenCloseLock (the lock closeCamera() also
        // holds) so the session is atomically either stored for a live camera or closed here,
        // and never assigned after a concurrent closeCamera() has finished tearing down - which
        // would leave the session unreferenced and never closed. The lock is released before
        // setRepeatingRequest so a camera call is never made while holding it.
        try {
            cameraOpenCloseLock.acquire()
        } catch (e: InterruptedException) {
            Thread.currentThread().interrupt()
            session.close()
            return
        }
        try {
            if (cameraDevice == null) {
                session.close()
                return
            }
            captureSession = session
        } finally {
            cameraOpenCloseLock.release()
        }

        // Even under the lock the surfaces can be released between here and setRepeatingRequest,
        // so the IllegalArgumentException catch remains the safety net for that request targeting
        // a surface released by closeCamera().
        handleCaptureException("Failed to request capture") {
            try {
                session.setRepeatingRequest(previewRequest, captureCallback, backgroundHandler)
            } catch (e: IllegalArgumentException) {
                logger.error("Failed to request capture", e)
            }
        }
    }

    private fun createCaptureSessionCompat(
        camera: CameraDevice,
        imageSurface: Surface,
        qrSurface: Surface,
        previewSurface: Surface,
        stateCallback: CameraCaptureSession.StateCallback,
    ) {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.P) {
            val executor =
                backgroundExecutor
                    ?: run {
                        maybeStartExecutorService()
                        backgroundExecutor
                    }
                    ?: return
            val sessionConfig =
                SessionConfiguration(
                    SessionConfiguration.SESSION_REGULAR,
                    listOf(
                        OutputConfiguration(imageSurface),
                        OutputConfiguration(qrSurface),
                        OutputConfiguration(previewSurface),
                    ),
                    executor,
                    stateCallback,
                )
            camera.createCaptureSession(sessionConfig)
        } else {
            @Suppress("DEPRECATION")
            camera.createCaptureSession(listOf(imageSurface, qrSurface, previewSurface), stateCallback, null)
        }
    }

    private fun captureStillImage() {
        if (isCapturing) return
        isCapturing = true
        val camera =
            cameraDevice
                ?: run {
                    isCapturing = false
                    return
                }
        val reader =
            imageReader
                ?: run {
                    isCapturing = false
                    return
                }

        handleCaptureException("Failed to capture still image") {
            val captureBuilder =
                camera.createCaptureRequest(CameraDevice.TEMPLATE_STILL_CAPTURE).apply {
                    addTarget(reader.surface)
                    set(CaptureRequest.CONTROL_AF_MODE, CaptureRequest.CONTROL_AF_MODE_CONTINUOUS_PICTURE)
                    set(CaptureRequest.JPEG_ORIENTATION, getJpegOrientation())
                }
            captureSession?.capture(captureBuilder.build(), stillCaptureCallback, backgroundHandler)
        }
    }

    @VisibleForTesting
    internal val onImageAvailableListener = ImageReader.OnImageAvailableListener { reader ->
        processImage(reader)
    }

    @VisibleForTesting
    internal val qrImageAvailableListener = ImageReader.OnImageAvailableListener { reader ->
        if (cameraMode.value != CameraMode.QR) {
            // Drain to keep the YUV buffer queue from filling up and stalling the camera pipeline.
            reader.acquireLatestImage()?.close()
            return@OnImageAvailableListener
        }
        if (!qrInFlight.compareAndSet(false, true)) return@OnImageAvailableListener
        val image = reader.acquireLatestImage()
        if (image == null) {
            qrInFlight.set(false)
            return@OnImageAvailableListener
        }
        try {
            val result = qrAnalyzer.analyze(image)
            if (!result.isNullOrEmpty()) {
                mainHandler.post { handleQrResult(result) }
            }
        } finally {
            image.close()
            qrInFlight.set(false)
        }
    }

    @VisibleForTesting
    internal fun handleModeChanged(newMode: CameraMode) {
        if (cameraMode.value == newMode || (isQrOnly && newMode != CameraMode.QR)) return
        cameraMode.value = newMode
        if (newMode == CameraMode.QR) {
            qrAnalyzer.reset()
            qrResultSent = false
        }
        updatePreviewRequest()
        if (isAdded) {
            setFragmentResult(
                MODE_CHANGED_REQUEST_KEY,
                Bundle().apply { putString(RESULT_CAMERA_MODE, newMode.name) },
            )
        }
    }

    @VisibleForTesting
    internal fun handleQrResult(qrString: String) {
        if (qrResultSent || cameraMode.value != CameraMode.QR) return
        qrResultSent = true
        val bundle = Bundle().apply { putString(RESULT_QR_STRING, qrString) }
        if (isAdded) {
            setFragmentResult(RESULT_REQUEST_KEY, bundle)
        }
    }

    @VisibleForTesting
    internal fun processImage(reader: ImageReader) {
        val image = reader.acquireLatestImage() ?: return
        try {
            val buffer = image.planes[0].buffer
            val bytes = ByteArray(buffer.remaining())
            buffer.get(bytes)

            val ctx = context?.applicationContext ?: return
            val imageDir = File(ctx.cacheDir, LENS_IMAGES_DIR)
            imageDir.mkdirs()
            val imageFile = File(imageDir, "lens_capture_${now()}.jpg")
            imageFile.writeBytes(bytes)

            val uri =
                getUriForFile(
                    ctx,
                    "${ctx.packageName}.lens.fileprovider",
                    imageFile,
                )

            mainHandler.post {
                isCapturing = false
                handleResult(uri)
            }
        } catch (e: IOException) {
            logger.error("Failed to save lens capture image", e)
            mainHandler.post {
                isCapturing = false
                handleResult(null)
            }
        } catch (e: SecurityException) {
            logger.error("Failed to get URI for lens capture image", e)
            mainHandler.post {
                isCapturing = false
                handleResult(null)
            }
        } finally {
            image.close()
        }
    }

    private fun getJpegOrientation(): Int {
        val deviceRotation = getScreenRotation() ?: Surface.ROTATION_0
        val degrees = ORIENTATIONS[deviceRotation] ?: 0
        return (sensorOrientation + degrees) % DEGREES_FULL_ROTATION
    }

    @VisibleForTesting
    internal fun requestGalleryPick() {
        if (isAdded) {
            setFragmentResult(RESULT_REQUEST_KEY, buildGalleryRequestBundle())
        }
    }

    @VisibleForTesting
    internal fun buildGalleryRequestBundle(): Bundle {
        val key =
            if (cameraMode.value == CameraMode.QR) {
                RESULT_QR_GALLERY_REQUEST
            } else {
                RESULT_GALLERY_REQUEST
            }
        return Bundle().apply { putBoolean(key, true) }
    }

    @VisibleForTesting
    internal fun handleResult(uri: Uri?) {
        val bundle =
            Bundle().apply {
                putParcelable(RESULT_IMAGE_URI, uri)
            }
        if (isAdded) {
            setFragmentResult(RESULT_REQUEST_KEY, bundle)
        }
    }

    private fun closeCamera() {
        try {
            cameraOpenCloseLock.acquire()
            captureSession?.close()
            captureSession = null
            cameraDevice?.close()
            cameraDevice = null
            imageReader?.close()
            imageReader = null
            qrImageReader?.close()
            qrImageReader = null
            surface?.release()
            surface = null
        } catch (e: InterruptedException) {
            throw IllegalStateException("Interrupted while trying to lock camera closing.", e)
        } catch (e: RejectedExecutionException) {
            logger.error("backgroundExecutor terminated", e)
        } finally {
            cameraOpenCloseLock.release()
        }
    }

    @VisibleForTesting
    internal fun configureTransform(viewWidth: Int, viewHeight: Int) {
        val size = previewSize ?: return
        val rotation = getScreenRotation() ?: Surface.ROTATION_0
        val matrix =
            LensPreviewTransform.forRotation(
                viewWidth = viewWidth,
                viewHeight = viewHeight,
                bufferSize = size,
                rotation = rotation,
                swapped = LensPreviewTransform.areDimensionsSwapped(rotation, sensorOrientation),
            )
        textureView?.setTransform(matrix)
    }

    @VisibleForTesting
    internal fun maybeStartBackgroundThread() {
        if (backgroundThread == null) {
            backgroundThread = HandlerThread("LensCameraBackground")
        }
        backgroundThread?.let {
            if (!it.isAlive) {
                it.start()
                backgroundHandler = Handler(it.looper)
            }
        }
    }

    @VisibleForTesting
    internal fun stopBackgroundThread() {
        backgroundThread?.quitSafely()
        try {
            backgroundThread?.join()
        } catch (e: InterruptedException) {
            logger.debug("Interrupted while stopping background thread", e)
        } finally {
            backgroundThread = null
            backgroundHandler = null
        }
    }

    @VisibleForTesting
    internal fun maybeStartExecutorService() {
        if (backgroundExecutor == null) {
            backgroundExecutor = Executors.newSingleThreadExecutor()
        }
    }

    private fun stopExecutorService() {
        backgroundExecutor?.shutdownNow()
        backgroundExecutor = null
    }

    @Suppress("TooGenericExceptionCaught")
    private fun handleCaptureException(msg: String, block: () -> Unit) {
        try {
            block()
        } catch (e: Exception) {
            when (e) {
                is CameraAccessException,
                is IllegalStateException -> logger.error(msg, e)
                else -> throw e
            }
        }
    }

    @VisibleForTesting
    internal fun getScreenRotation(): Int? {
        return if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            context?.display?.rotation
        } else {
            @Suppress("DEPRECATION") activity?.windowManager?.defaultDisplay?.rotation
        }
    }

    companion object {
        const val RESULT_REQUEST_KEY = "lens_camera_fragment_result_key"
        const val RESULT_IMAGE_URI = "lens_camera_image_uri"
        const val RESULT_GALLERY_REQUEST = "lens_camera_gallery_request"
        const val RESULT_QR_GALLERY_REQUEST = "lens_camera_qr_gallery_request"
        const val RESULT_QR_STRING = "lens_camera_qr_string"

        /** Fragment argument naming the [CameraMode] to open in when there is no saved state. */
        const val ARG_INITIAL_MODE = "lens_camera_initial_mode"

        /** Fragment argument locking the camera to [CameraMode.QR] and hiding the mode toggle, for QR-only scans. */
        const val ARG_QR_ONLY = "lens_camera_qr_only"

        /** Fragment result key emitted whenever the user switches [CameraMode], carrying [RESULT_CAMERA_MODE]. */
        const val MODE_CHANGED_REQUEST_KEY = "lens_camera_mode_changed_key"
        const val RESULT_CAMERA_MODE = "lens_camera_mode"

        private const val STATE_CAMERA_MODE = "camera_mode"
        private const val STATE_QR_RESULT_SENT = "qr_result_sent"

        private const val CAMERA_CLOSE_LOCK_TIMEOUT_MS = 2500L

        private val BY_AREA = compareBy<Size> { it.width.toLong() * it.height }

        // Widest ratio difference still treated as the same aspect ratio when picking a preview size.
        private const val ASPECT_RATIO_TOLERANCE = 0.02

        private const val DEGREES_FULL_ROTATION = 360

        private val ORIENTATIONS =
            mapOf(
                Surface.ROTATION_0 to ORIENTATION_0,
                Surface.ROTATION_90 to ORIENTATION_90,
                Surface.ROTATION_180 to ORIENTATION_180,
                Surface.ROTATION_270 to ORIENTATION_270,
            )

        /**
         * Picks the JPEG capture size. This is the sensor's maximum, deliberately: the guaranteed stream combination
         * that includes a preview, a YUV analysis stream and a still capture only covers JPEG at MAXIMUM, so capping
         * the capture size can take the whole session outside what the device promises to support. The uploader
         * downscales to its own long-edge limit anyway, so nothing downstream depends on the size chosen here.
         */
        @VisibleForTesting
        internal fun chooseCaptureSize(sizes: Array<Size>): Size {
            require(sizes.isNotEmpty()) { "No capture sizes available from camera" }
            return Collections.max(sizes.asList(), BY_AREA)
        }

        /**
         * Picks the YUV size for QR analysis: the supported size within the preview bounds whose area is closest to
         * [QrAnalyzer]'s target, so analysis costs about what it does in the standalone QR scanner. Requesting
         * [QrAnalyzer.YUV_WIDTH] x [QrAnalyzer.YUV_HEIGHT] directly relies on the platform substituting a supported
         * size, which not every device does.
         *
         * Sizes sharing [aspectRatio] within [ASPECT_RATIO_TOLERANCE] are preferred over sizes closer to the target
         * area, so the analysed frame is framed the same way as the preview the user is aiming with. On a device
         * offering YUV sizes in several ratios, a code near the edge of the viewfinder is otherwise outside the frame
         * that gets analysed.
         */
        @VisibleForTesting
        internal fun chooseQrSize(choices: Array<Size>?, maxWidth: Int, maxHeight: Int, aspectRatio: Size): Size {
            if (choices.isNullOrEmpty()) return Size(QrAnalyzer.YUV_WIDTH, QrAnalyzer.YUV_HEIGHT)
            val withinBounds = choices.filter { it.width <= maxWidth && it.height <= maxHeight }
            val candidates = withinBounds.ifEmpty { listOf(Collections.min(choices.asList(), BY_AREA)) }

            val ratio = aspectRatio.width.toDouble() / aspectRatio.height
            val ratioDelta = { size: Size -> abs(size.width.toDouble() / size.height - ratio) }
            val closest = candidates.minOf(ratioDelta)
            val matching = candidates.filter { ratioDelta(it) <= closest + ASPECT_RATIO_TOLERANCE }

            val target = QrAnalyzer.YUV_WIDTH.toLong() * QrAnalyzer.YUV_HEIGHT
            return Collections.min(matching, compareBy { abs(it.width.toLong() * it.height - target) })
        }

        /**
         * Picks the preview size: the smallest size that matches [aspectRatio] and covers the view, falling back to the
         * largest matching size when nothing covers it. Both of those stay within [maxWidth] x [maxHeight], because a
         * preview stream above those bounds is not a PREVIEW-size stream and can fail the whole capture session on
         * LIMITED and LEGACY devices; only the fallback for a device offering nothing within them at all can return a
         * larger size.
         *
         * Matching is on the ratio closest to [aspectRatio] within [ASPECT_RATIO_TOLERANCE] rather than an exact match,
         * because sensors routinely report a maximum capture size that is only approximately 4:3 or 16:9 (a Pixel 7's
         * 4080x3072 is 1.328, not 1.333). An exact match rejects every realistic preview size on those devices and
         * silently selects a tiny one.
         */
        @VisibleForTesting
        internal fun chooseOptimalSize(
            choices: Array<Size>,
            textureViewWidth: Int,
            textureViewHeight: Int,
            maxWidth: Int,
            maxHeight: Int,
            aspectRatio: Size,
        ): Size {
            require(choices.isNotEmpty()) { "No preview sizes available from camera" }
            val target = aspectRatio.width.toDouble() / aspectRatio.height
            val ratioDelta = { size: Size -> abs(size.width.toDouble() / size.height - target) }

            val withinBounds = choices.filter { it.width <= maxWidth && it.height <= maxHeight }
            // The device offers no size within the preview bounds at all; the smallest is the closest we get.
            if (withinBounds.isEmpty()) return Collections.min(choices.asList(), BY_AREA)

            val closest = withinBounds.minOf(ratioDelta)
            val matching = withinBounds.filter { ratioDelta(it) <= closest + ASPECT_RATIO_TOLERANCE }
            val bigEnough = matching.filter { it.width >= textureViewWidth && it.height >= textureViewHeight }
            return if (bigEnough.isNotEmpty()) {
                Collections.min(bigEnough, BY_AREA)
            } else {
                // Right aspect ratio, but nothing covers the view, so the preview is upscaled.
                Collections.max(matching, BY_AREA)
            }
        }

        @VisibleForTesting
        internal fun getDisplaySize(windowManager: android.view.WindowManager): Point {
            val size = Point()
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
                val windowMetrics = windowManager.currentWindowMetrics
                val windowInsets = WindowInsetsCompat.toWindowInsetsCompat(windowMetrics.windowInsets)
                val insets =
                    windowInsets.getInsetsIgnoringVisibility(
                        WindowInsetsCompat.Type.navigationBars() or WindowInsetsCompat.Type.displayCutout()
                    )
                val bounds: Rect = windowMetrics.bounds
                size.set(bounds.width() - insets.right - insets.left, bounds.height() - insets.top - insets.bottom)
            } else {
                @Suppress("DEPRECATION") windowManager.defaultDisplay.getSize(size)
            }
            return size
        }
    }
}
