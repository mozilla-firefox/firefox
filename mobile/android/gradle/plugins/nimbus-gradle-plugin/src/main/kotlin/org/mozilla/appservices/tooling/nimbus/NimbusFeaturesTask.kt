/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.appservices.tooling.nimbus

import org.gradle.api.DefaultTask
import org.gradle.api.GradleException
import org.gradle.api.file.ConfigurableFileCollection
import org.gradle.api.file.DirectoryProperty
import org.gradle.api.file.RegularFileProperty
import org.gradle.api.provider.Property
import org.gradle.api.tasks.CacheableTask
import org.gradle.api.tasks.Input
import org.gradle.api.tasks.InputFile
import org.gradle.api.tasks.InputFiles
import org.gradle.api.tasks.Internal
import org.gradle.api.tasks.LocalState
import org.gradle.api.tasks.Optional
import org.gradle.api.tasks.OutputDirectory
import org.gradle.api.tasks.PathSensitive
import org.gradle.api.tasks.PathSensitivity
import org.gradle.api.tasks.TaskAction
import org.gradle.process.ExecOperations
import java.io.File
import javax.inject.Inject

/**
 * Generates Kotlin data classes for the features in a Nimbus manifest.
 *
 * If `applicationServicesDir` is set, the task runs `nimbus-fml` from that
 * Application Services checkout; otherwise, it runs the prebuilt `fmlBinary`.
 */
@CacheableTask
abstract class NimbusFeaturesTask : DefaultTask() {
    @get:Inject
    abstract val execOperations: ExecOperations

    @get:Internal
    abstract val applicationServicesDir: Property<String>

    // `@InputFiles` instead of `@InputFile` because we don't want
    // the task to fail if the `fmlBinary` file doesn't exist
    // (https://github.com/gradle/gradle/issues/2016).
    @get:InputFiles
    @get:Optional
    @get:PathSensitive(PathSensitivity.NONE)
    abstract val fmlBinary: RegularFileProperty

    @get:InputFile
    @get:PathSensitive(PathSensitivity.RELATIVE)
    abstract val inputFile: RegularFileProperty

    /**
     * The .fml.yaml files that the manifest loads transitively. Gradle uses this list to know when to run the task
     * again. It is not passed to `nimbus-fml`.
     */
    @get:InputFiles
    @get:PathSensitive(PathSensitivity.RELATIVE)
    abstract val manifestDependencies: ConfigurableFileCollection

    @get:Input
    abstract val channel: Property<String>

    @get:LocalState
    abstract val cacheDir: DirectoryProperty

    @get:OutputDirectory
    abstract val outputDir: DirectoryProperty

    @TaskAction
    fun generate() {
        outputDir.get().asFile.listFiles()?.forEach {
            if (!it.deleteRecursively()) throw GradleException("Could not delete $it")
        }
        execOperations.exec {
            val localAppServices = applicationServicesDir.orNull
            if (localAppServices == null) {
                val fmlBinaryFile = fmlBinary.orNull?.asFile
                if (fmlBinaryFile == null || !fmlBinaryFile.exists()) {
                    throw GradleException(
                        "`nimbus-fml` binary not found${fmlBinaryFile?.let { " at $it" }.orEmpty()}. Either build the project " +
                        "with `./mach build` or set `autoPublish.application-services.dir` in local.properties."
                    )
                }
                commandLine(fmlBinaryFile)
            } else {
                val cargoManifest = File(localAppServices, "components/support/nimbus-fml/Cargo.toml")
                commandLine("cargo", "run", "--manifest-path", cargoManifest, "--")
            }
            args("generate", "--language", "kotlin", "--channel", channel.get(), "--cache-dir", cacheDir.get().asFile)
            args(inputFile.get().asFile, outputDir.get().asFile)
        }
    }
}
