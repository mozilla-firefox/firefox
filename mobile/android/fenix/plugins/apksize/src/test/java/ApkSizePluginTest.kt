/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import java.io.File
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.util.zip.ZipEntry
import java.util.zip.ZipOutputStream
import org.json.JSONObject
import org.junit.jupiter.api.Assertions.assertEquals
import org.junit.jupiter.api.Assertions.assertFalse
import org.junit.jupiter.api.Test
import org.junit.jupiter.api.assertThrows
import org.junit.jupiter.api.io.TempDir

// Dex header layout, from https://source.android.com/docs/core/runtime/dex-format#header-item
private const val DEX_MAGIC = "dex\n"
private const val DEX_VERSION = "035\u0000"
private const val CLASS_DEFS_SIZE_OFFSET = 0x60

private const val DEX_FILE_SIZE = 0x200
private const val OTHER_DEX_FILE_SIZE = 0x300
private const val NATIVE_LIBRARY_SIZE = 0x400

class ApkSizePluginTest {
    @TempDir
    lateinit var tempDir: File

    private fun dexFile(classes: Int, size: Int = DEX_FILE_SIZE): ByteArray {
        val buffer = ByteBuffer.allocate(size).order(ByteOrder.LITTLE_ENDIAN)
        buffer.put(DEX_MAGIC.toByteArray())
        buffer.put(DEX_VERSION.toByteArray())
        buffer.putInt(CLASS_DEFS_SIZE_OFFSET, classes)
        return buffer.array()
    }

    private fun apk(vararg entries: Pair<String, ByteArray>): File {
        val file = File(tempDir, "app.apk")
        ZipOutputStream(file.outputStream()).use { zip ->
            entries.forEach { (name, bytes) ->
                zip.putNextEntry(ZipEntry(name))
                zip.write(bytes)
                zip.closeEntry()
            }
        }
        return file
    }

    @Test
    fun `reads the class count and size from a single dex file`() {
        val stats = determineDexStats(apk("classes.dex" to dexFile(classes = 3)))

        assertEquals(3, stats.classCount)
        assertEquals(DEX_FILE_SIZE.toLong(), stats.uncompressedSize)
    }

    @Test
    fun `sums every dex file and ignores other entries`() {
        val stats = determineDexStats(
            apk(
                "classes.dex" to dexFile(classes = 1),
                "classes2.dex" to dexFile(classes = 2, size = OTHER_DEX_FILE_SIZE),
                "assets/classes3.dex" to dexFile(classes = 50),
                "lib/arm64-v8a/libxul.so" to ByteArray(NATIVE_LIBRARY_SIZE),
            ),
        )

        assertEquals(
            DexStats(
                uncompressedSize = (DEX_FILE_SIZE + OTHER_DEX_FILE_SIZE).toLong(),
                compressedSize = stats.compressedSize,
                classCount = 3,
            ),
            stats,
        )
    }

    @Test
    fun `fails on an entry without the dex magic`() {
        assertThrows<IllegalStateException> {
            determineDexStats(apk("classes.dex" to ByteArray(DEX_FILE_SIZE)))
        }
    }

    @Test
    fun `fails on an apk without dex files`() {
        assertThrows<IllegalStateException> {
            determineDexStats(apk("lib/arm64-v8a/libxul.so" to ByteArray(NATIVE_LIBRARY_SIZE)))
        }
    }

    @Test
    fun `reports apk and dex suites`() {
        val json = buildPerfherderJson(
            "Nightly",
            mapOf("app-arm64-v8a-nightly.apk" to 140L, "app-x86_64-nightly.apk" to 145L),
            DexStats(
                uncompressedSize = 28,
                compressedSize = 12,
                classCount = 3,
            ),
        )

        val suites = json.getJSONArray("suites")
        assertEquals(2, suites.length())
        assertEquals("apk-size-Nightly", suites.getJSONObject(0).getString("name"))
        assertEquals(140, suites.getJSONObject(0).getLong("value"))

        val dex = suites.getJSONObject(1)
        assertEquals("dex-size-Nightly", dex.getString("name"))
        assertEquals(12, dex.getLong("value"))
        assertFalse(dex.has("shouldAlert"))
        assertEquals(
            mapOf(
                "compressed-size" to 12L,
                "uncompressed-size" to 28L,
                "class-count" to 3L,
            ),
            subtestValues(dex),
        )
    }

    @Test
    fun `does not alert on debug builds`() {
        val json = buildPerfherderJson("Debug", mapOf("app-debug.apk" to 1L), DexStats(1, 1, 1))

        val suites = json.getJSONArray("suites")
        assertFalse(suites.getJSONObject(0).getBoolean("shouldAlert"))
        assertFalse(suites.getJSONObject(1).getBoolean("shouldAlert"))
    }

    private fun subtestValues(suite: JSONObject): Map<String, Long> {
        val subtests = suite.getJSONArray("subtests")
        return (0 until subtests.length()).associate {
            val subtest = subtests.getJSONObject(it)
            subtest.getString("name") to subtest.getLong("value")
        }
    }
}
