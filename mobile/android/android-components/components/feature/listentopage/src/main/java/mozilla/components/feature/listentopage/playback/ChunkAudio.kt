/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.listentopage.playback

import java.io.File

/**
 * Which chunk of the article each audio file holds, and which of them are worth keeping.
 *
 * @param cache Where the files live.
 */
internal class ChunkAudio(private val cache: AudioFileCache) {

    private val files = mutableMapOf<Int, File>()

    /**
     * The audio for [chunkIndex], or `null` when it has not been synthesized, has been thrown away, or has been
     * reclaimed by the system.
     */
    suspend fun fileFor(chunkIndex: Int): File? {
        val file = files[chunkIndex] ?: return null
        if (cache.exists(file)) return file

        // Remove the reference so that we ensure the file still exists next time we want it
        files.remove(chunkIndex)

        return null
    }

    /**
     * Records that [file] holds the audio for [chunkIndex].
     *
     * A chunk synthesized twice, which a seek back into a deleted part of the article causes, replaces its file and the
     * one before it is deleted. Keeping both would leave the first behind for as long as the system keeps the cache.
     */
    suspend fun add(chunkIndex: Int, file: File) {
        files.put(chunkIndex, file)?.let { if (it != file) cache.delete(it) }
    }

    suspend fun discard(chunkIndex: Int) {
        files.remove(chunkIndex)?.let { cache.delete(it) }
    }

    /**
     * Deletes the audio of every chunk outside [window], and forgets it.
     *
     * Forgetting is the half that matters as much as deleting: a caller asks [fileFor] whether a chunk still has audio,
     * and a file that has been deleted has to answer no or the player is handed a path to nothing.
     */
    suspend fun keepOnly(window: IntRange) {
        val leaving = files.keys.filterNot { it in window }

        leaving.forEach { index -> files.remove(index)?.let { cache.delete(it) } }
    }

    /** Deletes everything this holds, and forgets it. */
    suspend fun clear() = keepOnly(IntRange.EMPTY)
}
