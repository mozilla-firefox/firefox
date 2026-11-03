/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.conventions

import org.gradle.api.DefaultTask
import org.gradle.api.GradleException
import org.gradle.api.file.DirectoryProperty
import org.gradle.api.file.RegularFileProperty
import org.gradle.api.tasks.CacheableTask
import org.gradle.api.tasks.InputFile
import org.gradle.api.tasks.OutputDirectory
import org.gradle.api.tasks.PathSensitive
import org.gradle.api.tasks.PathSensitivity
import org.gradle.api.tasks.TaskAction
import java.io.File
import java.util.UUID

/**
 * Writes the `build-metadata.properties` asset, so that crash reports can reference the build's
 * R8 mapping file.
 *
 * It records the mapping's SHA-256 (`r8.pg_map_hash`) and a UUID derived from it (`proguard.uuid`).
 */
@CacheableTask
abstract class WriteBuildMetadata : DefaultTask() {
    @get:InputFile
    @get:PathSensitive(PathSensitivity.NONE)
    abstract val mappingFile: RegularFileProperty

    @get:OutputDirectory
    abstract val outputDir: DirectoryProperty

    @TaskAction
    fun write() {
        val hash = mappingHash(mappingFile.get().asFile)
        val uuid = proguardUuid(hash)
        val output = outputDir.get().asFile
        output.deleteRecursively()
        output.mkdirs()
        output.resolve(FILENAME).writeText("$PROGUARD_UUID_PROPERTY=$uuid\n$PG_MAP_HASH_PROPERTY=$hash\n")
    }

    /**
     * Derives the mapping's UUID from its SHA-256 the same way as sentry-android-gradle-plugin, a
     * name-based (version 3) UUID of the hex string, so that both produce the same UUID.
     */
    private fun proguardUuid(mappingHash: String): UUID = UUID.nameUUIDFromBytes(mappingHash.toByteArray())

    /** Returns the SHA-256 that R8 records in the mapping header. */
    private fun mappingHash(file: File): String {
        file.bufferedReader().useLines { lines ->
            lines.take(HEADER_SEARCH_LINES).forEach { line ->
                if (line.startsWith(PG_MAP_HASH_PREFIX)) {
                    val hash = line.removePrefix(PG_MAP_HASH_PREFIX)
                    if (!SHA_256_HEX.matches(hash)) {
                        throw GradleException("Malformed '$PG_MAP_HASH_PREFIX' line in ${file.absolutePath}: $line")
                    }
                    return hash
                }
            }
        }
        throw GradleException("No '$PG_MAP_HASH_PREFIX' line in the header of ${file.absolutePath}")
    }

    companion object {
        const val FILENAME = "build-metadata.properties"
        private const val PROGUARD_UUID_PROPERTY = "proguard.uuid"
        private const val PG_MAP_HASH_PROPERTY = "r8.pg_map_hash"
        private const val PG_MAP_HASH_PREFIX = "# pg_map_hash: SHA-256 "
        private const val HEADER_SEARCH_LINES = 20
        private val SHA_256_HEX = Regex("[0-9a-f]{64}")
    }
}
