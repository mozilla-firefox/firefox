/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.components.lens

import android.Manifest
import android.content.Context
import android.content.Intent
import android.content.pm.PackageManager
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.graphics.ImageDecoder
import android.net.Uri
import android.os.Build
import android.os.Bundle
import android.widget.Toast
import androidx.activity.result.PickVisualMediaRequest
import androidx.activity.result.contract.ActivityResultContracts
import androidx.activity.viewModels
import androidx.annotation.VisibleForTesting
import androidx.appcompat.app.AppCompatActivity
import androidx.compose.ui.platform.ComposeView
import androidx.core.content.ContextCompat
import androidx.core.net.toUri
import androidx.core.os.BundleCompat
import androidx.core.view.OneShotPreDrawListener
import androidx.core.view.isVisible
import androidx.fragment.app.commit
import androidx.fragment.app.commitNow
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.ViewModelProvider
import androidx.lifecycle.lifecycleScope
import java.io.IOException
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import mozilla.components.feature.qr.QrAnalyzer
import mozilla.components.feature.qr.QrScanActivity
import mozilla.components.support.base.log.logger.Logger
import org.mozilla.fenix.BuildConfig
import org.mozilla.fenix.R
import org.mozilla.fenix.ext.components

internal const val LENS_IMAGES_DIR = "lens_images"

/**
 * Activity that hosts [LensCameraFragment] for capturing images for Google Lens. Handles camera permission and gallery
 * picking. A captured or photo picker image is uploaded here by [LensUploadOverlay] and returned as the Lens result
 * URL, so a cancelled or failed upload can fall back to the camera.
 */
class LensCameraActivity : AppCompatActivity() {

    private val logger = Logger("LensCameraActivity")

    private var optOutSheetPreDrawListener: OneShotPreDrawListener? = null

    // The camera permission is requested at most once per activity instance: the result is
    // delivered before onResume, so without this onResume would re-request it after a denial.
    @VisibleForTesting internal var permissionRequested = false

    private val requestPermissionLauncher =
        registerForActivityResult(ActivityResultContracts.RequestPermission()) { isGranted ->
            handlePermissionResult(isGranted)
        }

    @VisibleForTesting
    internal fun handlePermissionResult(isGranted: Boolean) {
        if (isGranted) {
            launchCameraFragment()
        } else {
            Toast.makeText(this, R.string.lens_camera_permission_denied, Toast.LENGTH_SHORT).show()
            cancelAndFinish()
        }
    }

    @VisibleForTesting internal var uploadViewModelFactory: ViewModelProvider.Factory? = null

    private val uploadViewModel: LensUploadViewModel by viewModels {
        uploadViewModelFactory
            ?: run {
                // Only the application context is captured: the view model outlives this activity.
                val appContext = applicationContext
                LensUploadViewModel.Factory(uploaderProvider = { appContext.components.lensImageUploader })
            }
    }

    @VisibleForTesting
    internal val uploadOverlay by lazy {
        LensUploadOverlay(
            activity = this,
            viewModel = uploadViewModel,
            overlay = findViewById(R.id.lens_upload_overlay),
            // Removing LensCameraFragment closes the camera and drops the fragment's back callback, which would
            // otherwise outrank the loading screen's and close the activity.
            removeCamera = {
                supportFragmentManager.findFragmentById(R.id.lens_fragment_container_view)?.let { cameraFragment ->
                    supportFragmentManager.commitNow { remove(cameraFragment) }
                }
            },
            // Added right away so a failure message is shown over the camera, not over the empty container.
            restoreCamera = {
                launchCameraFragment()
                supportFragmentManager.executePendingTransactions()
            },
        )
    }

    private val galleryLauncher =
        registerForActivityResult(ActivityResultContracts.PickVisualMedia()) { uri -> handleGalleryResult(uri) }

    @VisibleForTesting
    internal fun handleGalleryResult(uri: Uri?) {
        if (uri == null || isQrOnly) return
        uploadOverlay.start(
            uri,
            isPrivate = intent.getBooleanExtra(EXTRA_IS_PRIVATE, false),
            source = IMAGE_SOURCE_PHOTO_PICKER,
        )
    }

    private val qrGalleryLauncher =
        registerForActivityResult(ActivityResultContracts.PickVisualMedia()) { uri ->
            if (uri != null) {
                decodeQrFromUri(uri)
            }
        }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_lens_camera)
        permissionRequested = savedInstanceState?.getBoolean(STATE_PERMISSION_REQUESTED, false) == true
        if (savedInstanceState == null && !isQrOnly) {
            lifecycleScope.launch(Dispatchers.IO) { clearLensImageCache() }
        }

        supportFragmentManager.setFragmentResultListener(
            GoogleLensOptOutBottomSheetFragment.RESULT_REQUEST_KEY,
            this,
        ) { _, bundle ->
            handleOptOutResult(bundle.getString(GoogleLensOptOutBottomSheetFragment.RESULT_ACTION))
        }

        supportFragmentManager.setFragmentResultListener(
            LensCameraFragment.MODE_CHANGED_REQUEST_KEY,
            this,
        ) { _, bundle ->
            CameraMode.fromNameOrNull(bundle.getString(LensCameraFragment.RESULT_CAMERA_MODE))?.let {
                components.settings.lensCameraLastMode = it
            }
        }

        supportFragmentManager.setFragmentResultListener(
            LensCameraFragment.RESULT_REQUEST_KEY,
            this,
        ) { _, bundle ->
            handleCameraResult(bundle)
        }

        uploadOverlay.observe()
    }

    private fun handleCameraResult(bundle: Bundle) {
        // RESULT_QR_STRING must never be set to an empty string by the producer
        // (LensCameraFragment.handleQrResult). An empty value falls through to the
        // gallery/image branch below; downstream callers treat an empty extra as no scan.
        val qrString = bundle.getString(LensCameraFragment.RESULT_QR_STRING)
        val imageUri: Uri? =
            BundleCompat.getParcelable(
                bundle,
                LensCameraFragment.RESULT_IMAGE_URI,
                Uri::class.java,
            )
        when {
            !qrString.isNullOrEmpty() -> {
                val resultIntent =
                    Intent().apply {
                        putExtra(EXTRA_SCAN_RESULT_DATA, qrString)
                    }
                setResult(RESULT_OK, resultIntent)
                finish()
            }

            bundle.getBoolean(LensCameraFragment.RESULT_QR_GALLERY_REQUEST, false) -> launchQrGalleryPicker()

            // QR-only scans skip the Google Lens opt-out sheet, so nothing may be sent to Google.
            isQrOnly -> {
                setResult(RESULT_CANCELED)
                finish()
            }

            bundle.getBoolean(LensCameraFragment.RESULT_GALLERY_REQUEST, false) -> launchGalleryPicker()

            imageUri != null ->
                uploadOverlay.start(
                    imageUri,
                    isPrivate = intent.getBooleanExtra(EXTRA_IS_PRIVATE, false),
                    source = IMAGE_SOURCE_CAMERA,
                )

            else -> {
                setResult(RESULT_CANCELED)
                finish()
            }
        }
    }

    override fun onSaveInstanceState(outState: Bundle) {
        super.onSaveInstanceState(outState)
        outState.putBoolean(STATE_PERMISSION_REQUESTED, permissionRequested)
    }

    override fun onResume() {
        super.onResume()
        // handlePermissionResult may already have finished us: activity results are delivered
        // before onResume, and finishing does not stop the lifecycle from reaching it.
        if (isFinishing) return

        // The opt-out sheet gates everything else: while it is up neither the camera permission nor
        // LensCameraFragment - and therefore the camera itself - is touched. QR-only scans never reach
        // Google, so they skip it.
        if (isQrOnly || components.settings.hasAcceptedGoogleLensFirstRun) {
            // A granted permission brings the camera back even if it was requested here: an upload removes the camera
            // fragment, so an upload lost with the process leaves no fragment to restore.
            if (!permissionRequested || hasCameraPermission()) {
                checkCameraPermission()
            }
        } else {
            showOptOutBackdrop()
            requestOptOutBottomSheet()
        }
    }

    override fun onStop() {
        super.onStop()
        // OneShotPreDrawListener only unregisters itself once it runs or once the view is detached,
        // and the decor view stays attached across a stop. Removing it here keeps a stop/start
        // before the first frame from leaving a stale listener that adds a second sheet.
        optOutSheetPreDrawListener?.removeListener()
        optOutSheetPreDrawListener = null
    }

    /**
     * Shows the camera-less backdrop the opt-out sheet sits on. Composed on demand so the common post-acknowledgement
     * path, which goes straight to the camera, never pays for it.
     */
    private fun showOptOutBackdrop() {
        val backdrop = findViewById<ComposeView>(R.id.lens_opt_out_backdrop)
        if (backdrop.isVisible) return
        // Not wrapped in FirefoxTheme, matching how LensCameraFragment hosts LensCameraScreen: the
        // camera surface is always dark and uses fixed colors.
        backdrop.setContent {
            LensOptOutBackdrop(onClose = ::cancelAndFinish)
        }
        backdrop.isVisible = true
    }

    /**
     * Hides the backdrop once the camera takes over. It is only covered by the fragment container, so leaving it in
     * place would keep its close button reachable by accessibility services.
     */
    private fun hideOptOutBackdrop() {
        val backdrop = findViewById<ComposeView>(R.id.lens_opt_out_backdrop)
        if (!backdrop.isVisible) return
        backdrop.isVisible = false
        backdrop.disposeComposition()
    }

    /**
     * Schedules the opt-out sheet for after the activity's first frame. Showing it any earlier adds the dialog window
     * while this window still has non-final metrics, which lays the sheet out at the top of the screen for a frame
     * before its behavior offsets it into place.
     */
    @VisibleForTesting
    internal fun requestOptOutBottomSheet() {
        if (
            optOutSheetPreDrawListener != null ||
                supportFragmentManager.findFragmentByTag(GoogleLensOptOutBottomSheetFragment.TAG) != null
        ) {
            return
        }
        optOutSheetPreDrawListener =
            OneShotPreDrawListener.add(window.decorView) {
                // Posted so the sheet is added after the first frame is drawn, not merely after layout.
                // The listener handle is cleared by onStop rather than here, so a request stays "in
                // flight" for the rest of this start and cannot be issued twice.
                window.decorView.post {
                    if (!isFinishing && !isDestroyed && lifecycle.currentState.isAtLeast(Lifecycle.State.STARTED)) {
                        showOptOutBottomSheet()
                    }
                }
            }
    }

    @VisibleForTesting
    internal fun showOptOutBottomSheet() {
        // DialogFragment.show commits asynchronously, so an already-pending add would be invisible
        // to findFragmentByTag and a second caller would queue a duplicate sheet behind it.
        supportFragmentManager.executePendingTransactions()
        if (supportFragmentManager.findFragmentByTag(GoogleLensOptOutBottomSheetFragment.TAG) != null) {
            return
        }
        GoogleLensOptOutBottomSheetFragment()
            .show(
                supportFragmentManager,
                GoogleLensOptOutBottomSheetFragment.TAG,
            )
    }

    @VisibleForTesting
    internal fun handleOptOutResult(action: String?) {
        when (action) {
            GoogleLensOptOutBottomSheetFragment.ACTION_TRY_IT_NOW -> {
                // Recorded even if the permission is subsequently denied: the user did opt in.
                components.settings.hasAcceptedGoogleLensFirstRun = true
                checkCameraPermission()
            }
            GoogleLensOptOutBottomSheetFragment.ACTION_SETTINGS -> {
                openSearchSettings()
                cancelAndFinish()
            }
            else -> cancelAndFinish()
        }
    }

    @VisibleForTesting
    internal fun openSearchSettings() {
        startActivity(
            Intent(
                    Intent.ACTION_VIEW,
                    "${BuildConfig.DEEP_LINK_SCHEME}://settings_search_engine".toUri(),
                )
                .apply {
                    setPackage(packageName)
                    flags = Intent.FLAG_ACTIVITY_CLEAR_TOP
                }
        )
    }

    @VisibleForTesting
    internal fun cancelAndFinish() {
        setResult(RESULT_CANCELED)
        finish()
    }

    @VisibleForTesting
    internal fun checkCameraPermission() {
        if (hasCameraPermission()) {
            launchCameraFragment()
        } else {
            permissionRequested = true
            requestPermissionLauncher.launch(Manifest.permission.CAMERA)
        }
    }

    private fun hasCameraPermission() =
        ContextCompat.checkSelfPermission(this, Manifest.permission.CAMERA) == PackageManager.PERMISSION_GRANTED

    private fun launchCameraFragment() {
        hideOptOutBackdrop()
        // onResume would otherwise bring the camera back underneath the upload after a rotation or a pause.
        if (uploadViewModel.state.value.coversCamera) {
            return
        }
        if (supportFragmentManager.findFragmentById(R.id.lens_fragment_container_view) != null) {
            return
        }
        supportFragmentManager.commit {
            add(
                R.id.lens_fragment_container_view,
                LensCameraFragment::class.java,
                Bundle().apply {
                    putString(LensCameraFragment.ARG_INITIAL_MODE, resolveInitialMode().name)
                    putBoolean(LensCameraFragment.ARG_QR_ONLY, isQrOnly)
                },
            )
        }
    }

    private val isQrOnly: Boolean
        get() = intent.getBooleanExtra(EXTRA_QR_ONLY, false)

    /**
     * [CameraMode.QR] when launched with [EXTRA_QR_ONLY], otherwise the mode requested through [EXTRA_INITIAL_MODE],
     * falling back to the one the user last selected.
     */
    @VisibleForTesting
    internal fun resolveInitialMode(): CameraMode {
        if (isQrOnly) return CameraMode.QR
        return CameraMode.fromNameOrNull(intent.getStringExtra(EXTRA_INITIAL_MODE))
            ?: components.settings.lensCameraLastMode
    }

    private fun launchGalleryPicker() {
        galleryLauncher.launch(PickVisualMediaRequest(ActivityResultContracts.PickVisualMedia.ImageOnly))
    }

    private fun launchQrGalleryPicker() {
        qrGalleryLauncher.launch(PickVisualMediaRequest(ActivityResultContracts.PickVisualMedia.ImageOnly))
    }

    @VisibleForTesting
    internal fun decodeQrFromUri(uri: Uri) {
        lifecycleScope.launch(Dispatchers.IO) {
            val qrString =
                try {
                    val bitmap = loadSoftwareBitmap(uri)
                    QrAnalyzer().analyze(bitmap)
                } catch (e: CancellationException) {
                    throw e
                } catch (e: IOException) {
                    logger.error("Failed to decode QR from picked image", e)
                    null
                } catch (e: IllegalArgumentException) {
                    logger.error("Failed to decode QR from picked image", e)
                    null
                } catch (e: IllegalStateException) {
                    logger.error("Failed to decode QR from picked image", e)
                    null
                }
            withContext(Dispatchers.Main) {
                if (!isFinishing && !isDestroyed) handleQrDecodeResult(qrString)
            }
        }
    }

    @VisibleForTesting
    internal fun handleQrDecodeResult(qrString: String?) {
        if (qrString.isNullOrEmpty()) {
            Toast.makeText(this, R.string.lens_camera_qr_no_code_found, Toast.LENGTH_SHORT).show()
            return
        }
        val resultIntent =
            Intent().apply {
                putExtra(EXTRA_SCAN_RESULT_DATA, qrString)
            }
        setResult(RESULT_OK, resultIntent)
        finish()
    }

    private fun loadSoftwareBitmap(uri: Uri): Bitmap {
        return if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.P) {
            val source = ImageDecoder.createSource(contentResolver, uri)
            ImageDecoder.decodeBitmap(source) { decoder, info, _ ->
                // QrAnalyzer.analyze(bitmap) calls Bitmap.getPixels, which throws on
                // hardware-backed bitmaps — force the software allocator.
                decoder.allocator = ImageDecoder.ALLOCATOR_SOFTWARE
                val longEdge = maxOf(info.size.width, info.size.height)
                if (longEdge > QR_DECODE_MAX_DIMENSION) {
                    val scale = QR_DECODE_MAX_DIMENSION.toFloat() / longEdge
                    decoder.setTargetSize(
                        (info.size.width * scale).toInt().coerceAtLeast(1),
                        (info.size.height * scale).toInt().coerceAtLeast(1),
                    )
                }
            }
        } else {
            decodeDownsampledStream(uri)
        }
    }

    @VisibleForTesting
    internal fun decodeDownsampledStream(uri: Uri): Bitmap {
        val bounds = BitmapFactory.Options().apply { inJustDecodeBounds = true }
        contentResolver.openInputStream(uri)?.use {
            BitmapFactory.decodeStream(it, null, bounds)
        }
        // A bounds pass always returns a null bitmap, so the decoded dimensions are what tell
        // us whether the image could be read. They are untouched if the stream could not be opened.
        if (bounds.outWidth <= 0 || bounds.outHeight <= 0) {
            throw IOException("Unable to read the bounds of $uri")
        }

        val longEdge = maxOf(bounds.outWidth, bounds.outHeight)
        var sampleSize = 1
        while (longEdge / sampleSize > QR_DECODE_MAX_DIMENSION) sampleSize *= 2

        val opts = BitmapFactory.Options().apply { inSampleSize = sampleSize }
        val stream = contentResolver.openInputStream(uri)
        return stream?.use { BitmapFactory.decodeStream(it, null, opts) }
            ?: throw IOException(if (stream == null) "Unable to open $uri" else "Unable to decode $uri")
    }

    @VisibleForTesting
    internal fun clearLensImageCache() {
        val imageDir = java.io.File(cacheDir, LENS_IMAGES_DIR)
        if (imageDir.exists()) {
            imageDir.listFiles()?.forEach { it.delete() }
        }
    }

    companion object {
        /**
         * Result-intent extra key carrying a decoded QR string produced by the in-camera QR mode. Re-exported from
         * [QrScanActivity] so Lens consumers don't need to depend on the QR component directly, while keeping the value
         * identical to the key [org.mozilla.fenix.components.QrScanFenixFeature] expects when the intent is forwarded
         * to it.
         */
        const val EXTRA_SCAN_RESULT_DATA = QrScanActivity.EXTRA_SCAN_RESULT_DATA

        // Cap the long edge of decoded gallery images before QR analysis. Modern phone photos
        // are 12 MP+ which would allocate ~50 MB as ARGB_8888 plus another ~50 MB for the
        // IntArray pixel copy inside QrAnalyzer — enough to OOM low-RAM devices. ZXing
        // detects QR codes reliably well below this resolution.
        private const val QR_DECODE_MAX_DIMENSION = 2048

        private const val STATE_PERMISSION_REQUESTED = "permission_requested"

        internal const val IMAGE_SOURCE_CAMERA = "camera"

        internal const val IMAGE_SOURCE_PHOTO_PICKER = "photo_picker"

        /**
         * Result intent extra carrying the Lens results URL of the image this activity uploaded. The search telemetry
         * has already been recorded.
         */
        internal const val EXTRA_LENS_RESULT_URL = "lens_result_url"

        /**
         * Launch and result intent extra naming the browsing mode an image is uploaded in. The result echoes the launch
         * value so the result tab always matches the cookie context of the upload.
         */
        internal const val EXTRA_IS_PRIVATE = "lens_is_private"

        @VisibleForTesting internal const val EXTRA_INITIAL_MODE = "lens_initial_mode"

        @VisibleForTesting internal const val EXTRA_QR_ONLY = "lens_qr_only"

        /**
         * Creates an intent to launch [LensCameraActivity].
         *
         * @param context The context to launch the activity from.
         * @param isPrivate Whether an image uploaded by this activity should use the private cookie context.
         * @param initialMode The [CameraMode] to open in, or null to reopen the mode the user last selected.
         */
        fun newIntent(context: Context, isPrivate: Boolean, initialMode: CameraMode? = null): Intent {
            return Intent(context, LensCameraActivity::class.java).apply {
                putExtra(EXTRA_IS_PRIVATE, isPrivate)
                initialMode?.let { putExtra(EXTRA_INITIAL_MODE, it.name) }
            }
        }

        /**
         * Creates an intent to launch [LensCameraActivity] as a QR code scanner only: the camera opens in
         * [CameraMode.QR], the mode toggle is hidden and the Google Lens opt-out sheet is skipped.
         *
         * @param context The context to launch the activity from.
         */
        fun newQrScannerIntent(context: Context): Intent {
            return Intent(context, LensCameraActivity::class.java).putExtra(EXTRA_QR_ONLY, true)
        }
    }
}
