/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.appservices.tooling.nimbus

import org.gradle.api.GradleException
import org.gradle.api.artifacts.transform.InputArtifact
import org.gradle.api.artifacts.transform.TransformAction
import org.gradle.api.artifacts.transform.TransformOutputs
import org.gradle.api.artifacts.transform.TransformParameters
import org.gradle.api.file.FileSystemLocation
import org.gradle.api.provider.Property
import org.gradle.api.provider.Provider
import org.gradle.api.provider.ProviderFactory
import org.gradle.api.tasks.Input
import org.gradle.api.tasks.PathSensitive
import org.gradle.api.tasks.PathSensitivity
import java.util.zip.ZipFile

/**
 * Extracts the `nimbus-fml` binary for one platform from the
 * application-services archive, which holds the binaries for all platforms.
 */
abstract class NimbusFmlBinaryTransform : TransformAction<NimbusFmlBinaryTransform.Parameters> {
    interface Parameters : TransformParameters {
        /** The platform string (e.g. "x86_64-pc-windows-gnu"). */
        @get:Input
        val platform: Property<String>
    }

    @get:InputArtifact
    @get:PathSensitive(PathSensitivity.NONE)
    abstract val archive: Provider<FileSystemLocation>

    override fun transform(outputs: TransformOutputs) {
        val archiveFile = archive.get().asFile
        val platform = parameters.platform.get()
        val pattern = Regex("${Regex.escape(platform)}[^/]*/release/nimbus-fml[^/]*")
        ZipFile(archiveFile).use { zip ->
            val matches = zip.entries().asSequence().filter { pattern.matches(it.name) }.toList()
            val entry = matches.singleOrNull()
                ?: throw GradleException("Expected one nimbus-fml binary for $platform in $archiveFile, found: ${matches.map { it.name }}")
            val binary = outputs.file(entry.name.substringAfterLast('/'))
            zip.getInputStream(entry).use { input -> binary.outputStream().use { input.copyTo(it) } }
            binary.setExecutable(true)
        }
    }

    companion object {
        fun detectPlatform(providers: ProviderFactory): Provider<String> {
            return providers.systemProperty("os.name").zip(providers.systemProperty("os.arch")) { osName, osArch ->
                val os = osName.lowercase()
                val arch = when (osArch.lowercase()) {
                    "x86_64", "amd64" -> "x86_64"
                    "aarch64" -> "aarch64"
                    else -> null
                }
                when {
                    os.contains("win") -> "x86_64-pc-windows-gnu"
                    arch != null && os.contains("linux") -> "$arch-unknown-linux"
                    arch != null && os.contains("mac") -> "$arch-apple-darwin"
                    else -> throw GradleException("No nimbus-fml binary for os.name=$osName, os.arch=$osArch")
                }
            }
        }
    }
}
