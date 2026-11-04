/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.perf

import android.Manifest
import android.content.ClipData
import android.content.ClipboardManager
import android.content.Context
import android.content.pm.PackageManager
import android.os.Build
import android.util.Base64
import androidx.core.content.ContextCompat
import java.io.File
import java.io.FileOutputStream
import java.io.IOException
import mozilla.components.concept.fetch.MutableHeaders
import mozilla.components.concept.fetch.Request
import mozilla.components.concept.fetch.Response
import org.json.JSONObject
import org.mozilla.fenix.R
import org.mozilla.fenix.ext.components

private const val PROFILER_API = "https://api.profiler.firefox.com/compressed-store"
private const val PROFILER_SERVER_HEADER = "application/vnd.firefox-profiler+json;version=1.0"
private const val TOKEN = "profileToken"
private const val PROFILER_DATA_URL = "https://profiler.firefox.com/public/"

// IMPORTANT NOTE: Please keep the profiler presets in sync with their Firefox Desktop counterparts:
// https://searchfox.org/mozilla-central/rev/c130c69b7b863d5e28ab9524b65c27c7a9507c48/devtools/client/performance-new/shared/background.jsm.js#121

private val firefox_features =
    arrayOf(
        "screenshots",
        "js",
        "stackwalk",
        "java",
        "processcpu",
        "ipcmessages",
        "memory",
    )
private val firefox_threads =
    arrayOf(
        "GeckoMain",
        "Compositor",
        "Renderer",
        "SwComposite",
        "DOM Worker",
    )

private val graphics_features = arrayOf("stackwalk", "js", "java", "processcpu", "ipcmessages", "memory")
private val graphics_threads =
    arrayOf(
        "GeckoMain",
        "Compositor",
        "Renderer",
        "SwComposite",
        "RenderBackend",
        "GlyphRasterizer",
        "SceneBuilder",
        "WrWorker",
        "TextureUpdate",
        // Image decoding threads: most formats decode on the TaskController
        // thread pool, JPEG XL additionally uses its own pool.
        "TaskController",
        "JxlDecode",
    )

private val media_features =
    arrayOf(
        "js",
        "stackwalk",
        "audiocallbacktracing",
        "ipcmessages",
        "processcpu",
        "java",
        "memory",
    )
private val media_threads =
    arrayOf(
        "cubeb",
        "audio",
        "BackgroundThreadPool",
        "camera",
        "capture",
        "Compositor",
        "decoder",
        "GeckoMain",
        "gmp",
        "graph",
        "grph",
        "InotifyEventThread",
        "IPDL Background",
        "media",
        "ModuleProcessThread",
        "PacerThread",
        "RemVidChild",
        "RenderBackend",
        "Renderer",
        "Socket Thread",
        "SwComposite",
        "webrtc",
        "TextureUpdate",
    )

private val networking_features =
    arrayOf(
        "screenshots",
        "js",
        "stackwalk",
        "java",
        "processcpu",
        "bandwidth",
        "ipcmessages",
        "memory",
    )

private val networking_threads =
    arrayOf(
        "Compositor",
        "DNS Resolver",
        "DOM Worker",
        "GeckoMain",
        "Renderer",
        "Socket Thread",
        "StreamTrans",
        "SwComposite",
        "TRR Background",
    )

private val debug_features =
    arrayOf(
        "java",
        "ipcmessages",
        "js",
        "markersallthreads",
        "processcpu",
        "samplingallthreads",
        "stackwalk",
        "unregisteredthreads",
        "flows",
    )

private val debug_threads = arrayOf("*")

private val web_compat_features =
    arrayOf(
        "java",
        "screenshots",
        "js",
        "stackwalk",
        "nostacksampling",
        "tracing",
    )

private val web_compat_threads =
    arrayOf(
        "GeckoMain",
        "DOM Worker",
    )

/** Profiler settings enum for grouping features and settings together */
enum class ProfilerSettings(val threads: Array<String>, val features: Array<String>) {
    Firefox(firefox_threads, firefox_features),
    Graphics(graphics_threads, graphics_features),
    Media(media_threads, media_features),
    Networking(networking_threads, networking_features),
    Debug(debug_threads, debug_features),
    WebCompat(web_compat_threads, web_compat_features);

    init {
        require(features.contains("java")) {
            "ProfilerSettings.$name must include the 'java' feature."
        }
    }
}

/** Collection of functionality to parse and save the profile returned by GeckoView. */
object ProfilerUtils {

    /**
     * Saves a profile to the Firefox Profiler server and returns the public URL.
     *
     * @param profileResult The compressed profile data as a ByteArray from GeckoView
     * @param context Android context for accessing network services
     * @return The public URL where the profile can be viewed
     * @throws IOException If network upload fails or temporary file operations fail
     */
    fun saveProfileUrlToClipboard(profileResult: ByteArray, context: Context): String {
        // The profile is saved to a temporary file since our fetch API takes a file or a string.
        // Converting the ByteArray to a String would hurt the encoding, which we need to preserve.
        // The file may potentially contain sensitive data and should be handled carefully.
        val outputFile = createTemporaryFile(profileResult, context)
        try {
            val response = networkCallToProfilerServer(outputFile, context)
            val profileToken = decodeProfileToken(response)
            return PROFILER_DATA_URL + profileToken
        } finally {
            outputFile.delete()
        }
    }

    /**
     * Completes the profile saving process by copying the URL to clipboard and showing confirmation.
     *
     * @param context Android context for accessing clipboard service
     * @param url The profiler URL to copy to clipboard
     * @param onUrlFinish Callback function that receives a string resource ID for user feedback
     */
    fun finishProfileSave(context: Context, url: String, onUrlFinish: (Int) -> Unit) {
        val clipboardManager = context.getSystemService(Context.CLIPBOARD_SERVICE) as ClipboardManager
        val clipData = ClipData.newPlainText("Profiler URL", url)
        clipboardManager.setPrimaryClip(clipData)
        onUrlFinish(R.string.profiler_uploaded_url_to_clipboard)
    }

    private fun createTemporaryFile(profileResult: ByteArray, context: Context): File {
        val outputDir = context.cacheDir
        val outputFile = File.createTempFile("tempProfile", ".json", outputDir)
        FileOutputStream(outputFile).use { fileOutputStream ->
            fileOutputStream.write(profileResult)
            return outputFile
        }
    }

    private fun networkCallToProfilerServer(outputFile: File, context: Context): Response {
        val request =
            Request(
                url = PROFILER_API,
                method = Request.Method.POST,
                headers = MutableHeaders("Accept" to PROFILER_SERVER_HEADER),
                body = Request.Body.fromFile(outputFile),
                conservative = true,
            )
        return context.components.core.client.fetch(request)
    }

    private fun decodeProfileToken(response: Response): String {
        val jwtToken = StringBuilder()
        response.body.useBufferedReader {
            val jwt = it.readText()
            val jwtSplit = jwt.split(".")
            val decodedBytes = Base64.decode(jwtSplit[1], Base64.DEFAULT)
            val decodedStr = decodedBytes.decodeToString()
            val jsonObject = JSONObject(decodedStr)
            jwtToken.append(jsonObject[TOKEN])
        }
        return jwtToken.toString()
    }

    /**
     * Checks if the app has notification permission on Android 13+ (API level 33+).
     *
     * This permission check is required for profiler notifications on newer Android versions. On older versions,
     * notification permission is granted by default.
     *
     * @param context Android context for permission checking
     * @return true if notification permission is granted or not required, false otherwise
     */
    fun hasNotificationPermission(context: Context): Boolean {
        return if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            ContextCompat.checkSelfPermission(
                context,
                Manifest.permission.POST_NOTIFICATIONS,
            ) == PackageManager.PERMISSION_GRANTED
        } else {
            true
        }
    }
}
