/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.support.ktx.android.net

import android.content.ContentResolver
import android.content.Context
import android.net.Uri
import android.os.Build
import android.provider.OpenableColumns
import android.webkit.MimeTypeMap
import androidx.annotation.VisibleForTesting
import androidx.core.net.toUri
import java.io.File
import java.io.FileOutputStream
import java.io.IOException
import java.io.InputStream
import java.util.UUID
import mozilla.components.support.base.log.logger.Logger
import mozilla.components.support.ktx.kotlin.sanitizeFileName

internal val commonPrefixes = listOf("www.", "mobile.", "m.")
internal val mobileSubdomains = listOf("mobile", "m")

/** Returns the host without common prefixes like "www" or "m". */
val Uri.hostWithoutCommonPrefixes: String?
    get() {
        val host = host ?: return null
        for (prefix in commonPrefixes) {
            if (host.startsWith(prefix)) return host.substring(prefix.length)
        }
        return host
    }

/**
 * Checks that the Uri has the same host as [other], with mobile subdomains removed.
 *
 * @param other The Uri to be compared.
 */
fun Uri.sameHostWithoutMobileSubdomainAs(other: Uri): Boolean {
    val thisHost =
        hostWithoutCommonPrefixes?.let {
            it.split(".").filter { subdomain ->
                mobileSubdomains.none { mobileSubdomain -> mobileSubdomain == subdomain }
            }
        } ?: return false
    val otherHost =
        other.hostWithoutCommonPrefixes?.let {
            it.split(".").filter { subdomain ->
                mobileSubdomains.none { mobileSubdomain -> mobileSubdomain == subdomain }
            }
        } ?: return false
    return thisHost == otherHost
}

/** Returns true if the [Uri] uses the "http" or "https" protocol scheme. */
val Uri.isHttpOrHttps: Boolean
    get() = scheme == "http" || scheme == "https"

/**
 * Checks that the given URL is in one of the given URL [scopes].
 *
 * https://www.w3.org/TR/appmanifest/#dfn-within-scope
 *
 * @param scopes Uris that each represent a scope. A Uri is within the scope if the origin matches and it starts with
 *   the scope's path.
 * @return True if this Uri is within any of the given scopes.
 */
fun Uri.isInScope(scopes: Iterable<Uri>): Boolean {
    val path = path.orEmpty()
    return scopes.any { scope ->
        sameOriginAs(scope) && path.startsWith(scope.path.orEmpty())
    }
}

/** Checks that Uri has the same scheme and host as [other]. */
fun Uri.sameSchemeAndHostAs(other: Uri) = scheme == other.scheme && host == other.host

/**
 * Checks that Uri has the same origin as [other].
 *
 * https://html.spec.whatwg.org/multipage/origin.html#same-origin
 */
fun Uri.sameOriginAs(other: Uri) = sameSchemeAndHostAs(other) && port == other.port

/** Indicate if the [this] uri is under the application private directory. */
fun Uri.isUnderPrivateAppDirectory(context: Context): Boolean {
    return when (this.scheme) {
        ContentResolver.SCHEME_FILE -> {
            try {
                val uriPath = path ?: return true
                val uriCanonicalPath = File(uriPath).canonicalPath
                val dataDirCanonicalPath = File(context.applicationInfo.dataDir).canonicalPath
                if (Build.VERSION.SDK_INT <= Build.VERSION_CODES.Q) {
                    uriCanonicalPath.startsWith(dataDirCanonicalPath)
                } else {
                    // We have to do this manual check on early builds of Android 11
                    // as symlink didn't resolve from /data/user/ to data/data
                    // we have to revise this again once Android 11 is out
                    // https://github.com/mozilla-mobile/android-components/issues/7750
                    uriCanonicalPath.startsWith("/data/data") || uriCanonicalPath.startsWith("/data/user")
                }
            } catch (e: IOException) {
                true
            }
        }
        else -> false
    }
}

/**
 * Return a file name for [this] give Uri.
 *
 * @return A file name for the content, or generated file name if the URL is invalid or the type is unknown
 */
fun Uri.getFileName(contentResolver: ContentResolver): String {
    return when (this.scheme) {
        ContentResolver.SCHEME_FILE -> File(path ?: "").name.sanitizeFileName()
        ContentResolver.SCHEME_CONTENT -> getFileNameForContentUris(contentResolver)
        else -> {
            generateFileName(getFileExtension(contentResolver))
        }
    }
}

/**
 * Return a file extension for [this] give Uri (only supports content:// schemes).
 *
 * @return A file extension for the content, or empty string if the URL is invalid or the type is unknown
 */
fun Uri.getFileExtension(contentResolver: ContentResolver): String {
    return MimeTypeMap.getSingleton().getExtensionFromMimeType(contentResolver.getType(this)) ?: ""
}

/**
 * Copy the content of [this] [Uri] into a temporary file in the given [dirToCopy]
 *
 * @return A "file://" [Uri] which contains the content of [this] [Uri].
 */
fun Uri.toFileUri(context: Context, dirToCopy: String = "/temps"): Uri {
    val contentResolver = context.contentResolver
    val cacheUploadDirectory = File(context.cacheDir, dirToCopy)

    if (!cacheUploadDirectory.exists()) {
        cacheUploadDirectory.mkdir()
    }

    val temporalFile = File(cacheUploadDirectory, getFileName(contentResolver))
    @Suppress("TooGenericExceptionCaught")
    try {
        contentResolver.openInputStream(this)!!.use { inStream ->
            copyFile(temporalFile, inStream)
        }
    } catch (e: IOException) {
        Logger("Uri.kt").warn("Could not convert uri to file uri", e)
    } catch (e: RuntimeException) {
        Logger("Uri.kt").warn("Could not access file from uri", e)
    }
    return "file:///${Uri.encode(temporalFile.absolutePath)}".toUri()
}

@VisibleForTesting
internal fun copyFile(temporalFile: File, inStream: InputStream): Long {
    return FileOutputStream(temporalFile).use { outStream ->
        inStream.copyTo(outStream)
    }
}

@VisibleForTesting
@Suppress("TooGenericExceptionCaught")
internal fun Uri.getFileNameForContentUris(contentResolver: ContentResolver): String {
    var fileName = ""
    var fileExtension = ""

    try {
        fileExtension = getFileExtension(contentResolver)
        contentResolver.query(this, null, null, null, null)?.use { cursor ->
            val nameIndex = cursor.getColumnIndex(OpenableColumns.DISPLAY_NAME)
            if (nameIndex != -1 && cursor.moveToFirst()) {
                fileName = cursor.getString(nameIndex).orEmpty()
            }
        }
    } catch (e: RuntimeException) {
        Logger("Uri.kt").warn("Could not read the file name from the content provider", e)
    }

    return fileName.sanitizeFileName().ifEmpty { generateFileName(fileExtension) }
}

/** Generate a file name using a randomUUID + the current timestamp. */
@VisibleForTesting
internal fun generateFileName(
    fileExtension: String = "",
    timeStamp: Long = System.currentTimeMillis(),
): String {
    val randomId = UUID.randomUUID().toString().removePrefix("-").trim()
    return if (fileExtension.isNotEmpty()) {
        "$randomId$timeStamp.$fileExtension"
    } else {
        "$randomId$timeStamp"
    }
}

/**
 * Checks that the given URI is readable
 *
 * @param contentResolver the contentResolver that will be used to check the permission
 * @return true is the URI is readable
 */
@Suppress("TooGenericExceptionCaught")
fun Uri.isReadable(contentResolver: ContentResolver): Boolean {
    return try {
        val projection = arrayOf("_id") // Minimal projection
        val isReadable =
            contentResolver.query(this, projection, null, null, null)?.use {
                true
            } ?: false
        Logger.debug("Read permission was ${if (!isReadable) "not" else ""} granted on this URI")
        isReadable
    } catch (e: SecurityException) {
        Logger.debug("Read permission was not granted on this URI", e)
        false
    } catch (e: IllegalStateException) {
        Logger.debug("Unable to query URI (IllegalStateException)", e)
        false
    } catch (e: IllegalArgumentException) {
        Logger.debug("Unable to query URI (IllegalArgumentException)", e)
        false
    } catch (e: NullPointerException) {
        Logger.debug("Unable to query URI (NullPointerException)", e)
        false
    }
}
