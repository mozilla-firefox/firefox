/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.support.utils

import android.content.ContentResolver
import android.content.ContentUris
import android.net.Uri
import android.os.Environment
import android.provider.MediaStore
import android.webkit.MimeTypeMap
import java.io.ByteArrayOutputStream
import java.io.File
import java.io.UnsupportedEncodingException
import java.util.regex.Matcher
import java.util.regex.Pattern
import kotlin.text.replace
import mozilla.components.support.utils.DownloadUtils.CONTENT_DISPOSITION_TYPE
import mozilla.components.support.utils.DownloadUtils.fileNameAsteriskContentDispositionPattern
import mozilla.components.support.utils.ext.decodeIfNeeded

object DownloadUtils {

    /**
     * This is the regular expression to match the content disposition type segment.
     *
     * A content disposition header can start either with inline or attachment followed by comma; For example:
     * attachment; filename="filename.jpg" or inline; filename="filename.jpg" (inline|attachment)\\s*; -> Match either
     * inline or attachment, followed by zero o more optional whitespaces characters followed by a comma.
     */
    private const val CONTENT_DISPOSITION_TYPE = "(inline|attachment)\\s*;"

    /**
     * This is the regular expression to match filename* parameter segment.
     *
     * A content disposition header could have an optional filename* parameter, the difference between this parameter
     * and the filename is that this uses the encoding defined in RFC 5987.
     *
     * Some examples: filename*=utf-8''success.html filename*=iso-8859-1'en'file%27%20%27name.jpg
     * filename*=utf-8'en'filename.jpg
     *
     * For matching this section we use: \\s*filename\\s*=\\s*= -> Zero or more optional whitespaces characters followed
     * by filename followed by any zero or more whitespaces characters and the equal sign;
     *
     * (utf-8|iso-8859-1)-> Either utf-8 or iso-8859-1 encoding types.
     *
     * '[^']*'-> Zero or more characters that are inside of single quotes '' that are not single quote.
     *
     * (\S*) -> Zero or more characters that are not whitespaces. In this group, it's where we are going to have the
     * filename.
     */
    private const val CONTENT_DISPOSITION_FILE_NAME_ASTERISK =
        "\\s*filename\\*\\s*=\\s*(utf-8|iso-8859-1)'[^']*'([^;\\s]*)"

    /**
     * Format as defined in RFC 2616 and RFC 5987 Both inline and attachment types are supported. More details can be
     * found https://developer.mozilla.org/en-US/docs/Web/HTTP/Reference/Headers/Content-Disposition
     *
     * The first segment is the [CONTENT_DISPOSITION_TYPE], there you can find the documentation, Next, it's the
     * filename segment, where we have a filename="filename.ext" For example, all of these could be possible in this
     * section: filename="filename.jpg" filename="file\"name.jpg" filename="file\\name.jpg" filename="file\\\"name.jpg"
     * filename=filename.jpg
     *
     * For matching this section we use: \\s*filename\\s*=\\s*= -> Zero or more whitespaces followed by filename
     * followed by zero or more whitespaces and the equal sign.
     *
     * As we want to extract the the content of filename="THIS", we use:
     *
     * \\s* -> Zero or more whitespaces
     *
     * (\"((?:\\\\.|[^|"\\\\])*)\" -> A quotation mark, optional : or \\ or any character, and any non quotation mark or
     * \\\\ zero or more times.
     *
     * For example: filename="file\\name.jpg", filename="file\"name.jpg" and filename="file\\\"name.jpg"
     *
     * We don't want to match after ; appears, For example filename="filename.jpg"; foo we only want to match before the
     * semicolon, so we use. |[^;]*)
     *
     * \\s* -> Zero or more whitespaces.
     *
     * For supporting cases, where we have both filename and filename*, we use:
     * "(?:;$contentDispositionFileNameAsterisk)?"
     *
     * Some examples:
     *
     * attachment; filename="_.jpg"; filename*=iso-8859-1'en'file%27%20%27name.jpg attachment; filename="_.jpg";
     * filename*=iso-8859-1'en'file%27%20%27name.jpg
     */
    private val contentDispositionPattern =
        Pattern.compile(
            CONTENT_DISPOSITION_TYPE +
                "\\s*filename\\s*=\\s*(\"((?:\\\\.|[^\"\\\\])*)\"|[^;]*)\\s*" +
                "(?:;$CONTENT_DISPOSITION_FILE_NAME_ASTERISK)?",
            Pattern.CASE_INSENSITIVE,
        )

    /** This is an alternative content disposition pattern where only filename* is available */
    private val fileNameAsteriskContentDispositionPattern =
        Pattern.compile(
            CONTENT_DISPOSITION_TYPE + CONTENT_DISPOSITION_FILE_NAME_ASTERISK,
            Pattern.CASE_INSENSITIVE,
        )

    /** Keys for the capture groups inside contentDispositionPattern */
    private const val ENCODED_FILE_NAME_GROUP = 5
    private const val ENCODING_GROUP = 4
    private const val QUOTED_FILE_NAME_GROUP = 3
    private const val UNQUOTED_FILE_NAME = 2

    /** Belongs to the [fileNameAsteriskContentDispositionPattern] */
    private const val ALTERNATIVE_FILE_NAME_GROUP = 3
    private const val ALTERNATIVE_ENCODING_GROUP = 2

    /** Definition as per RFC 5987, section 3.2.1. (value-chars) */
    private val encodedSymbolPattern = Pattern.compile("%[0-9a-f]{2}|[0-9a-z!#$&+-.^_`|~]", Pattern.CASE_INSENSITIVE)

    /**
     * Maximum number of characters for the title length.
     *
     * Android OS is Linux-based and therefore would have the limitations of the linux filesystem it uses under the
     * hood. To the best of our knowledge, Android only supports EXT3, EXT4, exFAT, and EROFS filesystems. From these
     * three, the maximum file name length is 255.
     *
     * @see <a href="https://en.wikipedia.org/wiki/Comparison_of_file_systems#Limits"/>
     */
    private const val MAX_FILE_NAME_LENGTH = 255

    /** The generic file name [extractFileNameFromUrl] falls back to when no file name can be found. */
    const val DEFAULT_FILE_NAME = "downloadfile"

    /**
     * The maximum allowable length for a file name, including the directory path, file extension, and a version suffix
     * (e.g., "(1)"). This value is set to 250 to reserve space for a version suffix up to "(999)" and ensure the total
     * path length does not exceed the file system's limit of 255 characters.
     */
    private const val MAX_FILE_NAME_COPY_VERSION_LENGTH = 250

    /**
     * The maximum allowable length for a file extension, excluding the leading dot. If the extension exceeds this
     * length, it will be removed to prevent excessively long file names.
     */
    private const val MAX_FILE_EXTENSION_LENGTH = 14

    /**
     * The minimum allowable length for a truncated file name. Ensures that after truncation, a file retains some
     * recognizable portion of its name.
     */
    private const val MIN_FILE_NAME_LENGTH = 5

    const val SCHEME_CONTENT = "content://"

    /** The HTTP response code for a successful request. */
    const val RESPONSE_CODE_SUCCESS = 200

    // Some site add extra information after the mimetype, for example 'application/pdf; qs=0.001'
    // we just want to extract the mimeType and ignore the rest.
    fun sanitizeMimeType(mimeType: String?): String? {
        return (if (mimeType != null) {
                if (mimeType.contains(";")) {
                    mimeType.substringBefore(";")
                } else {
                    mimeType
                }
            } else {
                null
            })
            ?.trim()
    }

    /**
     * Truncates the file name if its length, combined with the directory path and file extension, exceeds the maximum
     * allowable path length. If the file extension is too long, it is removed entirely.
     *
     * @param baseFileName The base name of the file (excluding the extension).
     * @param fileExtension The file extension, that does not include the leading dot (e.g., "txt").
     * @param path The full path of the directory where the file will be created.
     * @return A pair containing the adjusted base file name and the adjusted file extension.
     */
    fun truncateFileName(baseFileName: String, fileExtension: String, path: String): Pair<String, String> {
        val totalLength = baseFileName.length + fileExtension.length + path.length + 1 // dot
        if (totalLength <= MAX_FILE_NAME_COPY_VERSION_LENGTH) {
            return Pair(baseFileName, fileExtension)
        }

        // If the extension is too long, truncate base file name at the first dot and remove the extension
        val shouldRemoveExtension = fileExtension.length > MAX_FILE_EXTENSION_LENGTH
        val adjustedExtension = if (shouldRemoveExtension) "" else fileExtension
        val adjustedBaseFileName =
            if (shouldRemoveExtension) {
                baseFileName.substringBefore('.')
            } else {
                baseFileName
            }

        // Compute the maximum allowed length for the base file name
        val adjustedExtensionWithDotLength = if (adjustedExtension.isNotEmpty()) adjustedExtension.length + 1 else 0
        val maxBaseFileNameLength =
            (MAX_FILE_NAME_COPY_VERSION_LENGTH - path.length - adjustedExtensionWithDotLength).coerceAtLeast(
                MIN_FILE_NAME_LENGTH
            )

        return Pair(
            adjustedBaseFileName.take(maxBaseFileNameLength),
            adjustedExtension,
        )
    }

    /**
     * Constructs a file name by appending an optional version number and extension.
     *
     * @param fileName The base name of the file.
     * @param copyVersionNumber An optional version number to be appended.
     * @param fileExtension An optional file extension to be appended.
     * @return A formatted file name with the base name, optional version number, and optional extension.
     */
    fun createFileName(fileName: String, copyVersionNumber: Int? = null, fileExtension: String? = null) =
        StringBuilder(fileName)
            .apply {
                copyVersionNumber?.let { append("($it)") }
                fileExtension?.takeIf { it.isNotEmpty() }?.let { append(".$it") }
            }
            .toString()

    /**
     * Create a Content Disposition formatted string with the receiver used as the filename and file extension set as
     * PDF.
     *
     * This is primarily useful for connecting the "Save to PDF" feature response to downloads.
     */
    fun makePdfContentDisposition(filename: String): String {
        val pdfExtension = ".pdf"
        return if (filename.endsWith(pdfExtension, ignoreCase = true)) {
                filename.substringBeforeLast('.')
            } else {
                filename
            }
            .take(MAX_FILE_NAME_LENGTH - pdfExtension.length)
            // Escapes existing quotes and apostrophes, and puts the filename in quotes
            .run {
                "attachment; filename=\"${"$this$pdfExtension".replace("\"", "\\\"").replace("'", "\\'")}\";"
            }
    }

    /**
     * Extracts a file name from either the Content-Disposition header or the URL.
     *
     * @param contentDisposition The 'Content-Disposition' HTTP header value, if available.
     * @param url The URL from which the file is being downloaded.
     * @return The extracted filename, or [DEFAULT_FILE_NAME] when no usable filename is found.
     */
    fun extractFileNameFromUrl(contentDisposition: String?, url: String?): String =
        extractFileNameFromUrlOrNull(contentDisposition, url) ?: DEFAULT_FILE_NAME

    /**
     * Extracts a file name from either the Content-Disposition header or the URL.
     *
     * @param contentDisposition The 'Content-Disposition' HTTP header value, if available.
     * @param url The URL from which the file is being downloaded.
     * @return The extracted filename, or null when no usable filename is found.
     */
    fun extractFileNameFromUrlOrNull(contentDisposition: String?, url: String?): String? {
        var filename: String? = null

        // Extract file name from content disposition header field
        if (contentDisposition != null) {
            filename = parseContentDisposition(contentDisposition)?.substringAfterLast('/')
        }

        // If all the other http-related approaches failed, use the plain uri
        if (filename.isNullOrEmpty()) {
            // If there is a query string strip it, same as desktop browsers
            val decodedUrl: String? = url?.decodeIfNeeded()?.substringBefore('?')
            if (decodedUrl?.endsWith('/') == false) {
                filename = decodedUrl.substringAfterLast('/')
            }
        }

        // Finally, if couldn't get filename from URI, get a generic filename
        if (filename.isNullOrEmpty() || filename.startsWith(".")) {
            return null
        }

        return filename
    }

    private fun parseContentDisposition(contentDisposition: String): String? {
        return try {
            val fileName =
                parseContentDispositionWithFileName(contentDisposition)
                    ?: parseContentDispositionWithFileNameAsterisk(contentDisposition)
            fileName?.decodeIfNeeded()
        } catch (_: IllegalStateException) {
            // This function is defined as returning null when it can't parse the header
            null
        } catch (_: UnsupportedEncodingException) {
            // Do nothing
            null
        }
    }

    private fun getUnencodedFileName(contentDisposition: String): String? {
        val marker = "utf-8''"
        val indexOfMarker = contentDisposition.indexOf(marker)

        if (indexOfMarker != -1) {
            val startIndex = indexOfMarker + marker.length
            val unencodedFileName = contentDisposition.substring(startIndex)
            val indexOfNextSemicolon = unencodedFileName.indexOf(';')
            if (indexOfNextSemicolon != -1) {
                return unencodedFileName.take(indexOfNextSemicolon)
            }
            return unencodedFileName
        }
        return null
    }

    @Suppress("ReturnCount")
    private fun parseContentDispositionWithFileName(contentDisposition: String): String? {
        val matcher = contentDispositionPattern.matcher(contentDisposition)
        if (!matcher.find()) {
            return null
        }

        val unencodedFileNameOverride = getUnencodedFileName(contentDisposition)
        val encodedFileName = matcher.group(ENCODED_FILE_NAME_GROUP)

        // If getUnencodedFileName provides a direct value, and it's different from
        // what might be the group containing the encoded fileName.
        // We assume that the file name is not encoded, and we are returning it.
        if (unencodedFileNameOverride != null && unencodedFileNameOverride != encodedFileName) {
            return unencodedFileNameOverride
        }

        val encoding = matcher.group(ENCODING_GROUP)

        if (encodedFileName != null && encoding != null) {
            return try {
                decodeHeaderField(encodedFileName, encoding)
            } catch (_: UnsupportedEncodingException) {
                null
            }
        }

        return extractQuotedOrUnquoted(matcher)
    }

    private fun extractQuotedOrUnquoted(matcher: Matcher): String? {
        val quotedFileName = matcher.group(QUOTED_FILE_NAME_GROUP)
        if (quotedFileName != null) {
            return quotedFileName.replace("\\\\(.)".toRegex(), "$1")
        }
        return matcher.group(UNQUOTED_FILE_NAME)
    }

    private fun parseContentDispositionWithFileNameAsterisk(contentDisposition: String): String? {
        val alternative = fileNameAsteriskContentDispositionPattern.matcher(contentDisposition)

        return if (alternative.find()) {
            val encoding = alternative.group(ALTERNATIVE_ENCODING_GROUP) ?: return null
            val fileName = alternative.group(ALTERNATIVE_FILE_NAME_GROUP) ?: return null
            decodeHeaderField(fileName, encoding)
        } else {
            null
        }
    }

    @Throws(UnsupportedEncodingException::class)
    private fun decodeHeaderField(field: String, encoding: String): String {
        val m = encodedSymbolPattern.matcher(field)
        val stream = ByteArrayOutputStream()

        while (m.find()) {
            val symbol = m.group()

            if (symbol.startsWith("%")) {
                stream.write(symbol.substring(1).toInt(radix = 16))
            } else {
                stream.write(symbol[0].code)
            }
        }

        return stream.toString(encoding)
    }

    /** Compare the filename extension with the mime type and change it if necessary. */
    fun changeExtension(filename: String, providedMimeType: String?): String {
        val file = File(filename)
        val mimeTypeMap = MimeTypeMap.getSingleton()
        val extensionFromMimeType = getExtensionFromMimeType(providedMimeType)
        if (providedMimeType == null || extensionFromMimeType == null) return filename

        val mimeTypeFromFilename = mimeTypeMap.getMimeTypeFromExtension(file.extension) ?: ""

        val fileHasPossibleExtension = filename.contains(extensionFromMimeType, ignoreCase = true)
        val isFileMimeTypeDifferentFromProvidedMimeType =
            !mimeTypeFromFilename.equals(
                providedMimeType,
                ignoreCase = true,
            )

        // Mimetypes could have multiple possible file extensions, for example: text/html could have
        // either .htm or .html extensions. Since [getExtensionFromMimeType]
        // we try to only rename when there is a clear indication the existing extension is wrong.
        return if (isFileMimeTypeDifferentFromProvidedMimeType && !fileHasPossibleExtension) {
            return "$file.$extensionFromMimeType"
        } else {
            filename
        }
    }

    /**
     * Get the file extension for a given MIME type. This function first checks the system mappings, if no extension is
     * found, checks for custom mappings.
     *
     * @param mimeType The MIME type to map.
     * @return The corresponding file extension or null if no mapping exists.
     */
    private fun getExtensionFromMimeType(mimeType: String?): String? {
        val mimeTypeMap = MimeTypeMap.getSingleton()
        return mimeTypeMap.getExtensionFromMimeType(mimeType)
            ?: when (mimeType) {
                "application/x-pdf" -> "pdf"
                else -> null
            }
    }

    /** Guess the extension for a file using the mime type. */
    fun createExtension(mimeType: String?): String {
        var extension: String? = null

        if (mimeType != null) {
            extension = getExtensionFromMimeType(mimeType)?.let { ".$it" }
        }
        if (extension == null) {
            extension =
                if (mimeType?.startsWith("text/", ignoreCase = true) == true) {
                    // checking startsWith to ignoring encoding value such as "text/html; charset=utf-8"
                    if (mimeType.startsWith("text/html", ignoreCase = true)) {
                        ".html"
                    } else {
                        ".txt"
                    }
                } else {
                    // If there's no mime type assume binary data
                    ".bin"
                }
        }

        return extension
    }

    /**
     * Checks if a file path corresponds to the default public "Downloads" directory.
     *
     * @return `true` if the path is the default downloads directory, `false` otherwise. It also returns `false` if the
     *   path is null, empty, or a "content://" URI.
     */
    fun String?.isDefaultDownloadDirectory(): Boolean {
        if (this.isNullOrEmpty() || this.startsWith(SCHEME_CONTENT)) {
            return false
        }
        val defaultDownloadsDir = Environment.getExternalStoragePublicDirectory(Environment.DIRECTORY_DOWNLOADS)
        return File(this).absolutePath == defaultDownloadsDir.absolutePath
    }

    /**
     * Queries MediaStore to find a file by its display name within a given collection.
     *
     * @param collection The MediaStore collection to search in (e.g., MediaStore.Downloads.getContentUri(...)).
     * @param fileName The display name of the file to find.
     * @return The content [Uri] of the file if found, otherwise null.
     */
    fun ContentResolver.findFileInMediaStore(collection: Uri, fileName: String): Uri? {
        val projection = arrayOf(MediaStore.Downloads._ID)
        val selection = "${MediaStore.Downloads.DISPLAY_NAME} = ?"
        val selectionArgs = arrayOf(fileName)

        query(collection, projection, selection, selectionArgs, null)?.use { cursor ->
            if (cursor.moveToFirst()) {
                val idColumn = cursor.getColumnIndexOrThrow(MediaStore.Downloads._ID)
                val id = cursor.getLong(idColumn)
                return ContentUris.withAppendedId(collection, id)
            }
        }
        return null
    }
}
