/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.appservices.tooling.nimbus

import org.gradle.api.file.ConfigurableFileCollection
import org.gradle.api.file.DirectoryProperty
import org.gradle.api.file.RegularFileProperty
import org.gradle.api.provider.Property
import org.gradle.api.tasks.CacheableTask
import org.gradle.api.tasks.Input
import org.gradle.api.tasks.InputFile
import org.gradle.api.tasks.InputFiles
import org.gradle.api.tasks.LocalState
import org.gradle.api.tasks.OutputDirectory
import org.gradle.api.tasks.PathSensitive
import org.gradle.api.tasks.PathSensitivity
import org.gradle.process.ExecSpec

@CacheableTask
abstract class NimbusFeaturesTask : NimbusFmlCommandTask(), NimbusTask {
    @get:InputFile
    @get:PathSensitive(PathSensitivity.RELATIVE)
    abstract override val inputFile: RegularFileProperty

    /**
     * The .fml.yaml files that the manifest loads transitively. Gradle uses this list to know when to run the task
     * again. It is not passed to `nimbus-fml`.
     */
    @get:InputFiles
    @get:PathSensitive(PathSensitivity.RELATIVE)
    abstract val manifestDependencies: ConfigurableFileCollection

    @get:InputFiles
    @get:PathSensitive(PathSensitivity.RELATIVE)
    abstract override val repoFiles: ConfigurableFileCollection

    @get:Input
    abstract val channel: Property<String>

    @get:LocalState
    abstract override val cacheDir: DirectoryProperty

    @get:OutputDirectory
    abstract val outputDir: DirectoryProperty

    override fun configureFmlCommand(spec: ExecSpec) {
        spec.apply {
            args("generate")

            args("--language", "kotlin")
            args("--channel", channel.get())
            args("--cache-dir", cacheDir.get())
            for (file in repoFiles) {
                args("--repo-file", file)
            }

            args(inputFile.get().asFile)
            args(outputDir.get().asFile)
        }
    }
}
