/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.appservices.tooling.nimbus

import org.gradle.api.Action
import org.gradle.api.DefaultTask
import org.gradle.api.GradleException
import org.gradle.api.file.ArchiveOperations
import org.gradle.api.file.DirectoryProperty
import org.gradle.api.file.FileVisitDetails
import org.gradle.api.file.RegularFileProperty
import org.gradle.api.model.ObjectFactory
import org.gradle.api.provider.ListProperty
import org.gradle.api.provider.Property
import org.gradle.api.provider.Provider
import org.gradle.api.provider.ProviderFactory
import org.gradle.api.tasks.CacheableTask
import org.gradle.api.tasks.Input
import org.gradle.api.tasks.Internal
import org.gradle.api.tasks.Nested
import org.gradle.api.tasks.OutputFile
import org.gradle.api.tasks.TaskAction
import java.io.File
import java.io.IOException
import java.net.HttpURLConnection
import java.net.URI
import java.security.MessageDigest
import javax.inject.Inject

/**
 * A task that fetches a prebuilt `nimbus-fml` binary for the current platform.
 *
 * Prebuilt binaries for all platforms are packaged into ZIP archives, and
 * published to sources like `archive.mozilla.org` (for releases) or
 * TaskCluster (for nightly builds).
 *
 * This task takes a variable number of inputs: a list of archive sources,
 * and a list of glob patterns to find the binary for the current platform
 * in the archive.
 *
 * The unzipped binary is this task's only output. This output is then used as
 * an optional input to the `NimbusFmlCommandTask`s.
 */
@CacheableTask
abstract class NimbusAssembleToolsTask : DefaultTask() {
    @get:Inject
    abstract val archiveOperations: ArchiveOperations

    @get:Inject
    abstract val providers: ProviderFactory

    @get:Nested
    abstract val fetchSpec: FetchSpec

    @get:Nested
    abstract val unzipSpec: UnzipSpec

    /**
     * The directory holding one cache subdirectory per application-services
     * version.
     *
     * Subdirectories other than [versionCacheDir] are deleted once this task has
     * cached a new archive.
     */
    @get:Internal
    abstract val cacheRootDir: DirectoryProperty

    /**
     * The subdirectory of [cacheRootDir] holding the archive for the
     * application-services version this task fetches.
     */
    @get:Internal
    abstract val versionCacheDir: DirectoryProperty

    /** The location of the fetched ZIP archive. */
    @get:Internal
    abstract val archiveFile: RegularFileProperty

    /**
     * The location of the fetched hash file, which contains the
     * archive's checksum.
     */
    @get:Internal
    abstract val hashFile: RegularFileProperty

    /** The location of the unzipped binary. */
    @get:OutputFile
    abstract val fmlBinary: RegularFileProperty

    /** The platform string (e.g. "x86_64-pc-windows-gnu"). */
    @get:Input
    abstract val platform: Property<String>

    /** Connection timeout in milliseconds. */
    @get:Internal
    abstract val connectTimeout: Property<Int>

    /** Read timeout in milliseconds. */
    @get:Internal
    abstract val readTimeout: Property<Int>

    /** Whether Gradle is offline, in which case a cache miss fails instead of downloading. */
    @get:Internal
    abstract val offline: Property<Boolean>

    init {
        platform.convention(detectPlatform(providers))
        connectTimeout.convention(30000)
        readTimeout.convention(60000)
        offline.convention(false)
        archiveFile.convention(versionCacheDir.file("nimbus-fml.zip"))
        hashFile.convention(versionCacheDir.file("nimbus-fml.sha256"))
    }

    /**
     * Configures the task to download the archive.
     *
     * @param action The configuration action.
     */
    fun fetch(action: Action<FetchSpec>) {
        action.execute(fetchSpec)
    }

    /**
     * Configures the task to extract the binary from the archive.
     *
     * @param action The configuration action.
     */
    fun unzip(action: Action<UnzipSpec>) {
        action.execute(unzipSpec)
    }

    @TaskAction
    fun assembleTools() {
        val binaryFile = fmlBinary.get().asFile
        val archiveFileObj = archiveFile.get().asFile
        val hashFileObj = hashFile.get().asFile

        // `hashFile` is only written once an archive's checksum has been verified,
        // and its directory is keyed by the application-services version, so a
        // non-empty hash file is proof that the sibling archive was verified. We
        // can reuse it without any network access.
        val cachedHash = if (hashFileObj.exists()) hashFileObj.readText().trim() else ""
        if (cachedHash.isNotEmpty()) {
            if (binaryFile.exists()) {
                logger.info("nimbus-fml binary is up-to-date")
                return
            }
            if (archiveFileObj.exists() &&
                computeSha256(archiveFileObj).equals(cachedHash, ignoreCase = true)
            ) {
                logger.info("Extracting nimbus-fml binary from cached archive")
                extractBinary(archiveFileObj)
                return
            }
        }

        if (offline.get()) {
            throw GradleException("nimbus-fml is not cached in ${versionCacheDir.get().asFile} and Gradle is offline")
        }

        val sources = (listOf(fetchSpec) + fetchSpec.fallbackSources.get()).map {
            Source(
                URI(it.archive.get()),
                URI(it.hash.get()),
                connectTimeout.get(),
                readTimeout.get()
            )
        }

        logger.info("Fetching nimbus-fml for platform: {}", platform.get())

        // Clear the version-specific cache directory before downloading a new archive
        val versionCacheDirObj = versionCacheDir.get().asFile
        if (versionCacheDirObj.exists()) {
            logger.info("Clearing stale cache at {}", versionCacheDirObj)
            versionCacheDirObj.deleteRecursively()
            versionCacheDirObj.mkdirs()
        }

        // Download the archive and verify with hash from the same source
        var successfulSource: Source? = null
        var sourceHash: String? = null
        for (source in sources) {
            try {
                sourceHash = source.fetchHashString()
            } catch (ignored: IOException) {
                continue
            }

            if (source.trySaveArchiveTo(archiveFileObj)) {
                successfulSource = source
                break
            }
        }

        if (successfulSource == null) {
            throw GradleException("Failed to fetch archive from any of: ${sources.map { "`${it.archiveURI}`" }.joinToString(", ")}")
        }

        val verifiedHash = checkNotNull(sourceHash) {
            "sourceHash should not be null after successful source fetch"
        }

        val actualHash = computeSha256(archiveFileObj)
        if (!actualHash.equals(verifiedHash, ignoreCase = true)) {
            archiveFileObj.delete()
            throw GradleException("Archive checksum mismatch! Expected: $verifiedHash, got: $actualHash")
        }
        hashFileObj.writeText(verifiedHash)

        extractBinary(archiveFileObj)

        pruneOtherVersionCaches(versionCacheDirObj)
    }

    /**
     * Deletes every subdirectory of [cacheRootDir] except [keep].
     *
     * Each archive holds the binaries for all platforms, so a cache left behind
     * by an application-services version bump costs tens of megabytes and nothing
     * else ever collects it. Failures are logged and ignored, so that losing a
     * race with a concurrent build can't fail this task.
     */
    private fun pruneOtherVersionCaches(keep: File) {
        val cacheRoot = cacheRootDir.orNull?.asFile ?: return
        if (keep.parentFile != cacheRoot) {
            logger.warn("Not pruning nimbus-fml caches: {} is not directly inside {}", keep, cacheRoot)
            return
        }
        val siblings = cacheRoot.listFiles() ?: return
        for (sibling in siblings) {
            if (!sibling.isDirectory || sibling == keep) {
                continue
            }
            logger.info("Removing stale nimbus-fml cache at {}", sibling)
            if (!sibling.deleteRecursively()) {
                logger.warn("Could not fully remove stale nimbus-fml cache at {}", sibling)
            }
        }
    }

    protected fun extractBinary(archiveFileObj: File) {
        val binaryFile = fmlBinary.get().asFile
        val zipTree = archiveOperations.zipTree(archiveFileObj)
        val visitedFilePaths = mutableListOf<String>()
        zipTree.matching {
            include(unzipSpec.includePatterns.get())
        }.visit {
            if (!isDirectory) {
                if (visitedFilePaths.isEmpty()) {
                    binaryFile.parentFile?.mkdirs()
                    copyTo(binaryFile)
                    binaryFile.setExecutable(true)
                }
                visitedFilePaths.add(relativePath.toString())
            }
        }

        if (visitedFilePaths.isEmpty()) {
            throw GradleException("Couldn't find any files in archive matching unzip spec: (${unzipSpec.includePatterns.get().joinToString(" | ") { "`$it`" }})")
        }

        if (visitedFilePaths.size > 1) {
            throw GradleException("Ambiguous unzip spec matched ${visitedFilePaths.size} files in archive: ${visitedFilePaths.joinToString(", ") { "`$it`" }}")
        }
    }

    /**
     * Specifies the source from which to fetch the archive and
     * its hash file.
     */
    abstract class FetchSpec : SourceSpec() {
        @get:Inject
        abstract val objectFactory: ObjectFactory

        @get:Nested
        abstract val fallbackSources: ListProperty<SourceSpec>

        /**
         * Configures a fallback to try if the archive can't be fetched
         * from this source.
         *
         * The task will try fallbacks in the order in which they're
         * configured.
         *
         * @param action The configuration action.
         */
        fun fallback(action: Action<SourceSpec>) {
            val spec = objectFactory.newInstance(SourceSpec::class.java)
            action.execute(spec)
            fallbackSources.add(spec)
        }
    }

    /** Specifies the URL of an archive and its hash file. */
    abstract class SourceSpec {
        @get:Input
        abstract val archive: Property<String>

        @get:Input
        abstract val hash: Property<String>
    }

    /**
     * Specifies which binary to extract from the fetched archive.
     *
     * The spec should only match one file in the archive. If the spec
     * matches multiple files in the archive, the task will fail.
     */
    abstract class UnzipSpec {
        @get:Input
        abstract val includePatterns: ListProperty<String>

        /**
         * Includes all files whose paths match the pattern.
         *
         * @param pattern An Ant-style glob pattern.
         * @see org.gradle.api.tasks.util.PatternFilterable.include
         */
        fun include(pattern: String) {
            includePatterns.add(pattern)
        }
    }

    /** A helper to fetch an archive and its hash file. */
    data class Source(
        val archiveURI: URI,
        val hashURI: URI,
        val connectTimeout: Int,
        val readTimeout: Int
    ) {
        fun trySaveArchiveTo(destination: File): Boolean {
            return try {
                saveURITo(archiveURI, destination)
                true
            } catch (ignored: IOException) {
                false
            }
        }

        fun fetchHashString(): String {
            val connection = hashURI.toURL().openConnection() as HttpURLConnection
            connection.connectTimeout = connectTimeout
            connection.readTimeout = readTimeout
            connection.instanceFollowRedirects = true
            connection.requestMethod = "GET"

            try {
                if (connection.responseCode != 200) {
                    throw IOException("HTTP ${connection.responseCode}: ${connection.responseMessage}")
                }
                return connection.inputStream.use { it.bufferedReader().readText().trim().split(Regex("\\s+"))[0] }
            } finally {
                connection.disconnect()
            }
        }

        private fun saveURITo(source: URI, destination: File) {
            val connection = source.toURL().openConnection() as HttpURLConnection
            connection.connectTimeout = connectTimeout
            connection.readTimeout = readTimeout
            connection.instanceFollowRedirects = true
            connection.requestMethod = "GET"

            try {
                if (connection.responseCode != 200) {
                    throw IOException("HTTP ${connection.responseCode}: ${connection.responseMessage}")
                }
                destination.parentFile?.mkdirs()
                connection.inputStream.use { from ->
                    destination.outputStream().use { out ->
                        from.copyTo(out)
                    }
                }
            } finally {
                connection.disconnect()
            }
        }
    }

    companion object {
        private fun detectPlatform(providers: ProviderFactory): Provider<String> {
            val osProvider = providers.systemProperty("os.name").map { it.lowercase() }
            val archProvider = providers.systemProperty("os.arch").map { it.lowercase() }

            return osProvider.zip(archProvider) { os, arch ->
                val osPart = when {
                    os.contains("win") -> "pc-windows-gnu"
                    os.contains("nix") || os.contains("nux") || os.contains("aix") -> "unknown-linux"
                    os.contains("mac") -> "apple-darwin"
                    else -> "unknown"
                }

                val archPart = when {
                    arch.contains("x86_64") -> "x86_64"
                    arch.contains("amd64") -> "x86_64"
                    arch.contains("aarch") -> "aarch64"
                    else -> "unknown"
                }
                "$archPart-$osPart"
            }
        }

        fun getBinaryName(platform: String): String {
            return if (platform.contains("windows")) {
                "nimbus-fml.exe"
            } else {
                "nimbus-fml"
            }
        }

        private fun computeSha256(file: File): String {
            val digest = MessageDigest.getInstance("SHA-256")
            file.inputStream().use { inputStream ->
                val buffer = ByteArray(8192)
                var read: Int
                while (inputStream.read(buffer).also { read = it } != -1) {
                    digest.update(buffer, 0, read)
                }
            }
            return digest.digest().joinToString("") { "%02x".format(it) }
        }
    }
}
