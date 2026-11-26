/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.listentopage.playback

import android.content.Context
import java.io.File
import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext

/** Where the synthesized audio is kept. */
interface AudioFileCache {
    /** Returns an empty file for [key], creating the directory if needed. */
    suspend fun create(key: String): File

    /** Whether [file] is still on disk with audio in it. */
    suspend fun exists(file: File): Boolean

    /** Deletes [file] if it is still there. */
    suspend fun delete(file: File)

    /** Deletes everything this cache holds. */
    suspend fun clear()
}

/**
 * An [AudioFileCache] in a subdirectory of the app's **cache** directory.
 *
 * The cache directory is deliberate: the system may reclaim it at any time, including during playback. Files here must
 * never be treated as durable.
 *
 * @param directoryProvider Supplies the directory to keep the audio in. It is only called on first use: resolving
 *   `Context.cacheDir` reads the disk, so doing it eagerly trips StrictMode on the main thread.
 * @param ioDispatcher The dispatcher the disk access runs on.
 */
class DirectoryAudioFileCache
internal constructor(
    directoryProvider: () -> File,
    private val ioDispatcher: CoroutineDispatcher = Dispatchers.IO,
) : AudioFileCache {

    constructor(context: Context) : this({ File(context.cacheDir, AUDIO_DIRECTORY) })

    private val directory: File by lazy(directoryProvider)

    override suspend fun create(key: String): File =
        withContext(ioDispatcher) {
            directory.mkdirs()
            File(directory, "$key.wav")
        }

    override suspend fun exists(file: File): Boolean =
        withContext(ioDispatcher) {
            file.isFile && file.length() > 0
        }

    override suspend fun delete(file: File) {
        withContext(ioDispatcher) { file.delete() }
    }

    override suspend fun clear() {
        withContext(ioDispatcher) { directory.deleteRecursively() }
    }

    private companion object {
        const val AUDIO_DIRECTORY = "listen-to-page"
    }
}
