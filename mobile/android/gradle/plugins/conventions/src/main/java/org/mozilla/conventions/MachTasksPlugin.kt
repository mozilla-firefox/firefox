/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.conventions

import org.gradle.api.GradleException
import org.gradle.api.Plugin
import org.gradle.api.Project
import org.gradle.api.file.RegularFile
import org.gradle.api.logging.LogLevel
import org.gradle.api.provider.MapProperty
import org.gradle.api.provider.Property
import org.gradle.api.provider.Provider
import org.gradle.api.provider.ValueSource
import org.gradle.api.provider.ValueSourceParameters
import org.gradle.api.services.BuildService
import org.gradle.api.services.BuildServiceParameters
import org.gradle.api.tasks.PathSensitivity
import java.io.File

abstract class MozconfigService : BuildService<MozconfigService.Params>, AutoCloseable {
    interface Params : BuildServiceParameters {
        val mozconfigParam: MapProperty<String, Any>
    }

    override fun close() {}

    fun getMozconfig(): Map<String, Any> = parameters.mozconfigParam.get()
}

data class InstallManifestData(
    val inputs: Set<String>,
    val patterns: List<List<String>>,
    val outputs: Set<String>
) : java.io.Serializable

abstract class InstallManifestDataValueSource : ValueSource<InstallManifestData, InstallManifestDataValueSource.Params> {
    interface Params : ValueSourceParameters {
        val manifestPath: Property<String>
    }

    override fun obtain(): InstallManifestData {
        val path = parameters.manifestPath.get()
        val manifestFile = File(path)
        return if (manifestFile.exists()) {
            val manifest = InstallManifest(path)
            InstallManifestData(
                inputs = manifest.getInputFiles(),
                patterns = manifest.patterns.values.map { listOf(it.base, it.pattern) },
                outputs = manifest.paths.keys
            )
        } else {
            InstallManifestData(emptySet(), emptyList(), emptySet())
        }
    }
}

/**
 * Registers Gradle tasks that invoke `mach` commands (configure, build faster, stage-package)
 * with input/output tracking for up-to-date checks and configuration cache support.
 */
class MachTasksPlugin : Plugin<Project> {
    override fun apply(project: Project) {
        val mozconfig = project.gradle.extensions.extraProperties["mozconfig"] as Map<*, *>
        val topsrcdir = mozconfig["topsrcdir"] as String
        val topobjdir = mozconfig["topobjdir"] as String
        val substs = mozconfig["substs"] as Map<*, *>

        val mozconfigFileProvider = createMozconfigFileProvider(project, topsrcdir)
        val mozconfigServiceProvider = registerMozconfigService(project, mozconfig)

        // List-valued substs (like GRADLE_MAVEN_REPOSITORIES) are originally comma-separated.
        // When inherited as environment variables, the List objects are converted to strings and
        // inadvertently become space-separated. We compute the normalized values once here and
        // apply them to each mach task to ensure that both the `./mach configure` and the Gradle
        // build's `machConfigure` entry points get identical envs.
        val providers = project.providers
        val substsForEnv = mozconfig["substs"] as Map<*, *>
        val normalizedMachEnv: Provider<Map<String, String>> = providers.provider {
            buildMap {
                substsForEnv.forEach { (key, value) ->
                    if (value is List<*> && key is String && providers.environmentVariable(key).isPresent) {
                        put(key, value.joinToString(","))
                    }
                }
                // Bug 2040992: Override PATH with the captured value from substs so any
                // reentrant ./mach invocation (notably :machConfigure) sees the same PATH
                // as the original ./mach configure, regardless of what the gradle daemon's
                // cached env happens to hold.
                (substsForEnv["PATH"] as? String)?.let { put("PATH", it) }
            }
        }

        // Capture the env vars that gate the Make-invoking tasks at configuration time (as
        // configuration-cache inputs) so the task onlyIf predicate is a plain map lookup at
        // execution time rather than an untracked System.getenv / Task.project access.
        val geckoOnlyIfEnv: Map<String, String?> = listOf(
            "GRADLE_INVOKED_WITHIN_MACH_BUILD",
            "AB_CD",
            "MOZ_CHROME_MULTILOCALE",
            "IS_LANGUAGE_REPACK",
        ).associateWith { providers.environmentVariable(it).orNull }

        registerMachConfigure(project, mozconfig, mozconfigFileProvider, normalizedMachEnv, topsrcdir, topobjdir, substs)
        registerMachBuildFaster(project, mozconfig, mozconfigServiceProvider, normalizedMachEnv, geckoOnlyIfEnv, topsrcdir, topobjdir, substs)
        registerMachStagePackage(project, mozconfig, mozconfigServiceProvider, normalizedMachEnv, geckoOnlyIfEnv, topsrcdir, topobjdir, substs)
    }

    private fun createMozconfigFileProvider(project: Project, topsrcdir: String): Provider<RegularFile> {
        val layout = project.layout
        // Track all possible mozconfig files to invalidate the configuration cache if any
        // of them appear, disappear, or change.
        val dotMozconfig = layout.projectDirectory.file("${topsrcdir}/.mozconfig")
        val mozconfig = layout.projectDirectory.file("${topsrcdir}/mozconfig")
        val hasDotMozconfig = project.providers.fileContents(dotMozconfig)
            .asText.map { true }.orElse(false).get()
        val hasMozconfig = project.providers.fileContents(mozconfig)
            .asText.map { true }.orElse(false).get()
        val mozconfigEnv = project.providers.environmentVariable("MOZCONFIG")
        if (mozconfigEnv.isPresent) {
            project.providers.fileContents(
                layout.projectDirectory.file(mozconfigEnv.get())
            ).asText.orElse("").get()
        }
        // Precedence matches find_mozconfig(): $MOZCONFIG env var, then .mozconfig, then mozconfig.
        return mozconfigEnv
            .map { envPath -> layout.projectDirectory.file(envPath) }
            .orElse(project.provider {
                if (hasDotMozconfig) dotMozconfig
                else if (hasMozconfig) mozconfig
                else null
            })
    }

    private fun registerMozconfigService(
        project: Project,
        mozconfig: Map<*, *>
    ): Provider<MozconfigService> {
        return project.gradle.sharedServices.registerIfAbsent("mozconfig", MozconfigService::class.java) {
            parameters.mozconfigParam.set(mozconfig.entries.associate { (key, value) -> key as String to value as Any })
        }
    }

    private fun registerMachConfigure(
        project: Project,
        mozconfig: Map<*, *>,
        mozconfigFileProvider: Provider<RegularFile>,
        normalizedMachEnv: Provider<Map<String, String>>,
        topsrcdir: String,
        topobjdir: String,
        substs: Map<*, *>
    ) {
        project.tasks.register("machConfigure", MachExec::class.java).configure {
            group = "mach"
            description = "Runs `./mach configure`"
            workingDir(topsrcdir)

            environment(normalizedMachEnv.get())

            commandLine(substs["PYTHON3"] as String)
            args("${topsrcdir}/mach")
            args("configure")

            inputs.file(mozconfigFileProvider).optional()

            inputs.files(project.fileTree("${topsrcdir}/python/mozboot/mozboot") {
                include("*android*")
                include("android-avds/**")
            })

            val configStatusDepsPath = "${topobjdir}/config_status_deps.in"
            val configStatusDepsContents = project.providers.fileContents(
                project.layout.projectDirectory.file(configStatusDepsPath)
            ).asText.orElse("")

            // This file may not exist on the first run, but is created by machConfigure.
            // It lists files that, when changed, should mark machConfigure not UP-TO-DATE.
            // Reading the provider here ensures the configuration cache is invalidated when
            // the file appears or changes, so we can track those inputs on the next run.
            // This can result in the configuration cache being built on two back to back runs,
            // but that's what we want, and the only way to ensure correct behavior.
            val configStatusDepsLines = configStatusDepsContents.get()
            if (configStatusDepsLines.isNotEmpty()) {
                val deps = configStatusDepsLines.lines().filter {
                    it.isNotBlank() && !it.contains("config.status") && !it.contains(".mozconfig.json")
                }
                inputs.files(deps)
            }

            outputs.file(configStatusDepsPath)
            outputs.file("${topobjdir}/config.status")
            outputs.file("${topobjdir}/faster/unified_install_dist_bin")

            // Backend dependency files track moz.build inputs
            val isArtifactBuild = substs["MOZ_ARTIFACT_BUILDS"].isTruthy()
            val backendDepsFile = if (isArtifactBuild) {
                "backend.FasterMake+RecursiveMakeBackend.in"
            } else {
                "backend.RecursiveMakeBackend.in"
            }
            outputs.file("${topobjdir}/${backendDepsFile}")

            // upToDateWhen is a negative predicate: if any check returns false, the task re-runs.
            outputs.upToDateWhen { File(topobjdir, "config.status").exists() }
            outputs.upToDateWhen { File(topobjdir, "faster/unified_install_dist_bin").exists() }
            outputs.upToDateWhen { File(topobjdir, backendDepsFile).exists() }

            standardOutput = System.out
            errorOutput = System.err
        }
    }

    private fun registerMachBuildFaster(
        project: Project,
        mozconfig: Map<*, *>,
        mozconfigServiceProvider: Provider<MozconfigService>,
        normalizedMachEnv: Provider<Map<String, String>>,
        geckoOnlyIfEnv: Map<String, String?>,
        topsrcdir: String,
        topobjdir: String,
        substs: Map<*, *>
    ) {
        project.tasks.register("machBuildFaster", MachExec::class.java).configure {
            group = "mach"
            description = "Runs `./mach build faster`"
            usesService(mozconfigServiceProvider)
            onlyIf { MachExec.geckoBinariesOnlyIf(this, mozconfigServiceProvider.get().getMozconfig(), geckoOnlyIfEnv) }
            dependsOn(project.tasks.named("machConfigure"))

            workingDir(topsrcdir)

            environment(normalizedMachEnv.get())

            commandLine(substs["PYTHON3"] as String)
            args("${topsrcdir}/mach")
            args("build")
            args("faster")

            // Add `-v` if we're running under `--info` (or `--debug`).
            if (project.logger.isEnabled(LogLevel.INFO)) {
                args("-v")
            }

            // In artifact builds, we use a hybrid backend that merges inputs from both backends.
            // In full builds, RecursiveMakeBackend (the full backend) tracks all input files,
            val isArtifactBuild = substs["MOZ_ARTIFACT_BUILDS"].isTruthy()
            val backendName = if (isArtifactBuild) {
                "FasterMake+RecursiveMakeBackend"
            } else {
                "RecursiveMakeBackend"
            }
            val backendDepsPath = "${topobjdir}/backend.${backendName}.in"
            val backendDepsContents = project.providers.fileContents(
                project.layout.projectDirectory.file(backendDepsPath)
            ).asText.orElse("")

            // This file is generated by machConfigure and may not exist on the first run.
            // Reading the provider here ensures the configuration cache is invalidated
            // when the file appears or changes, so we can track those inputs on the next run.
            val backendDepsLines = backendDepsContents.get()
            if (backendDepsLines.isNotEmpty()) {
                inputs.files(backendDepsLines.lines().filter { it.isNotBlank() })
                    .withPathSensitivity(PathSensitivity.RELATIVE).optional(true)
            }

            // The unified manifest is created by machConfigure and contains all dist/bin
            // install targets. Track its contents for configuration cache invalidation.
            val manifestPath = "${topobjdir}/faster/unified_install_dist_bin"
            inputs.file(manifestPath).optional(true)

            val distBinDir = "${topobjdir}/dist/bin"
            val objects = project.objects
            val providers = project.providers

            val manifestData = providers.of(InstallManifestDataValueSource::class.java) {
                parameters.manifestPath.set(manifestPath)
            }

            inputs.files(manifestData.map { it.inputs })
                .withPathSensitivity(PathSensitivity.RELATIVE).optional(true)

            inputs.files(manifestData.map { data ->
                data.patterns.map { (base, pattern) ->
                    objects.fileTree().from(base).apply {
                        include(pattern)
                    }
                }
            }).withPathSensitivity(PathSensitivity.RELATIVE).optional(true)

            outputs.dir(distBinDir)

            standardOutput = System.out
            errorOutput = System.err
        }
    }

    private fun registerMachStagePackage(
        project: Project,
        mozconfig: Map<*, *>,
        mozconfigServiceProvider: Provider<MozconfigService>,
        normalizedMachEnv: Provider<Map<String, String>>,
        geckoOnlyIfEnv: Map<String, String?>,
        topsrcdir: String,
        topobjdir: String,
        substs: Map<*, *>
    ) {
        project.tasks.register("machStagePackage", MachExec::class.java).configure {
            group = "mach"
            description = "Runs `./mach build stage-package`"
            usesService(mozconfigServiceProvider)
            onlyIf { MachExec.geckoBinariesOnlyIf(this, mozconfigServiceProvider.get().getMozconfig(), geckoOnlyIfEnv) }
            dependsOn(project.tasks.named("machBuildFaster"))

            workingDir(topobjdir)

            environment(normalizedMachEnv.get())

            commandLine(substs["PYTHON3"] as String)
            args("${topsrcdir}/mach")
            args("build")
            args("stage-package")

            val distBin = "${topobjdir}/dist/bin"
            val dllPrefix = substs["DLL_PREFIX"] as String
            val dllSuffix = substs["DLL_SUFFIX"] as String
            val requiredGeckoLibs = listOf("xul", "mozglue").map {
                "${distBin}/${dllPrefix}${it}${dllSuffix}"
            }

            doFirst {
                val missing = requiredGeckoLibs.filter { !File(it).exists() }
                if (missing.isNotEmpty()) {
                    throw GradleException(
                        "Required Gecko binaries are missing from the object directory:\n" +
                            missing.joinToString("\n") { "  - $it" } +
                            "\n\nThese native libraries are produced by a full build, not by Gradle.\n" +
                            "Run `./mach build` from the source directory first, then retry your Gradle build."
                    )
                }
            }

            inputs.files(project.tasks.named("machBuildFaster"))

            outputs.dir("${topobjdir}/dist/geckoview/assets")
            outputs.dir("${topobjdir}/dist/geckoview/lib")

            standardOutput = System.out
            errorOutput = System.err
        }
    }
}
