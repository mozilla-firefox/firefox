/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.listentopage.playback

import java.io.File
import kotlinx.coroutines.test.runTest
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder

class DirectoryAudioFileCacheTest {

    @get:Rule val temporaryFolder = TemporaryFolder()

    private fun cacheIn(directory: File) = DirectoryAudioFileCache({ directory })

    @Test
    fun `test that the directory is not resolved until the cache is used`() {
        var resolved = false

        DirectoryAudioFileCache({
            resolved = true
            temporaryFolder.root
        })

        assertFalse(resolved)
    }

    @Test
    fun `test that create returns a wav file in the cache directory`() = runTest {
        val directory = File(temporaryFolder.root, "audio")

        val file = cacheIn(directory).create("utterance-1")

        assertEquals("utterance-1.wav", file.name)
        assertEquals(directory, file.parentFile)
    }

    @Test
    fun `test that create makes the directory when it does not exist yet`() = runTest {
        val directory = File(temporaryFolder.root, "audio")

        val file = cacheIn(directory).create("utterance-1")

        assertTrue(directory.isDirectory)
        assertFalse(file.exists())
    }

    @Test
    fun `test that delete removes the file`() = runTest {
        val cache = cacheIn(temporaryFolder.root)
        val file = cache.create("utterance-1").apply { writeText("audio") }

        cache.delete(file)

        assertFalse(file.exists())
    }

    @Test
    fun `test that deleting a file that is already gone is not an error`() = runTest {
        val cache = cacheIn(temporaryFolder.root)
        val file = cache.create("utterance-1")

        cache.delete(file)

        assertFalse(file.exists())
    }

    @Test
    fun `test that clear removes every file the cache holds`() = runTest {
        val directory = File(temporaryFolder.root, "audio")
        val cache = cacheIn(directory)
        cache.create("utterance-1").writeText("audio")
        cache.create("utterance-2").writeText("audio")

        cache.clear()

        assertFalse(directory.exists())
    }

    @Test
    fun `test that the cache is usable again after it has been cleared`() = runTest {
        val cache = cacheIn(File(temporaryFolder.root, "audio"))
        cache.create("utterance-1").writeText("audio")
        cache.clear()

        val file = cache.create("utterance-2").apply { writeText("audio") }

        assertTrue(file.exists())
    }

    @Test
    fun `test that a file holding audio exists`() = runTest {
        val cache = cacheIn(temporaryFolder.root)
        val file = cache.create("utterance-1").apply { writeText("audio") }

        assertTrue(cache.exists(file))
    }

    @Test
    fun `test that a file the engine has not written yet does not exist`() = runTest {
        val cache = cacheIn(temporaryFolder.root)

        assertFalse(cache.exists(cache.create("utterance-1")))
    }

    @Test
    fun `test that an empty file does not exist`() = runTest {
        val cache = cacheIn(temporaryFolder.root)
        val file = cache.create("utterance-1").apply { createNewFile() }

        // An interrupted request leaves one of these behind, and the player fails on it as it does on no file at all.
        assertFalse(cache.exists(file))
    }

    @Test
    fun `test that a deleted file does not exist`() = runTest {
        val cache = cacheIn(temporaryFolder.root)
        val file = cache.create("utterance-1").apply { writeText("audio") }

        cache.delete(file)

        assertFalse(cache.exists(file))
    }

    @Test
    fun `test that a file does not exist once the cache has been cleared`() = runTest {
        val cache = cacheIn(File(temporaryFolder.root, "audio"))
        val file = cache.create("utterance-1").apply { writeText("audio") }

        cache.clear()

        assertFalse(cache.exists(file))
    }

    @Test
    fun `test that a file the system reclaimed does not exist`() = runTest {
        val directory = File(temporaryFolder.root, "audio")
        val cache = cacheIn(directory)
        val file = cache.create("utterance-1").apply { writeText("audio") }

        // Emptying the directory without the cache being asked is what the system does, and it is not announced.
        directory.deleteRecursively()

        assertFalse(cache.exists(file))
    }
}
