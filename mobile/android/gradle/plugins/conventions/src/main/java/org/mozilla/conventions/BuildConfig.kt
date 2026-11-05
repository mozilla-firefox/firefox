/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.conventions

import com.charleskorn.kaml.Yaml
import kotlinx.serialization.Serializable
import org.gradle.api.Action
import org.gradle.api.Plugin
import org.gradle.api.Project
import org.gradle.api.artifacts.component.ProjectComponentIdentifier
import org.gradle.api.initialization.Settings
import org.gradle.api.logging.Logging
import java.io.File
import java.time.LocalDateTime
import java.time.format.DateTimeFormatter
import org.gradle.api.tasks.Copy

private val buildIdLogger = Logging.getLogger("org.mozilla.conventions.BuildId")

// Mimic Python: open(os.path.join(buildconfig.topobjdir, 'buildid.h')).readline().split()[2]
fun getBuildId(topobjdir: String, mozBuildDate: String?): String {
    if (mozBuildDate != null) {
        if (mozBuildDate.length == 14) {
            return mozBuildDate
        }
        buildIdLogger.warn("Ignoring invalid MOZ_BUILD_DATE: $mozBuildDate")
    }

    return File(topobjdir, "buildid.h").readText().split(Regex("\\s+"))[2]
}

// Return a manifest version string that respects the Firefox version format,
// see: https://developer.mozilla.org/en-US/docs/Mozilla/Add-ons/WebExtensions/manifest.json/version#version_format
fun getManifestVersionString(componentsVersion: String, topobjdir: String, mozBuildDate: String?): String {
    // We assume that the `version.txt` file will always contain a version
    // string with at least two parts separated with a dot. Below, we extract
    // each part, and we make sure that there is no letter, e.g. `"0a2"` would
    // become `"0"`.
    val parts = componentsVersion.split(".").map { part ->
        part.split(Regex("[ab]"))[0]
    }

    // Per https://developer.mozilla.org/en-US/docs/Mozilla/Add-ons/WebExtensions/manifest.json/version,
    // each part can have up to 9 digits. Note the single `H` when formatting the output to avoid
    // leading zeros, which are not allowed.
    val buildDate = LocalDateTime.parse(
        getBuildId(topobjdir, mozBuildDate),
        DateTimeFormatter.ofPattern("yyyyMMddHHmmss"),
    )
    val dateAndTime = buildDate.format(DateTimeFormatter.ofPattern("yyyyMMdd.Hmmss"))

    return "${parts[0]}.${parts[1]}.$dateAndTime"
}

// Configure a reusable task for updating the version in manifest.json for modules that package
// a web extension. We automate this to make sure we never forget to update the version, either
// in local development or for releases. In both cases, we want to make sure the latest version
// of all extensions (including their latest changes) are installed on first start-up.
fun updateExtensionVersion(task: Copy, extDir: String, componentsVersion: String) {
    val mozconfig = task.project.gradle.extensions.extraProperties["mozconfig"] as Map<*, *>
    val topobjdir = mozconfig["topobjdir"] as String
    val mozBuildDate = task.project.providers.environmentVariable("MOZ_BUILD_DATE").orNull

    task.from(extDir)
    task.include("manifest.template.json")
    task.rename { "manifest.json" }
    task.into(extDir)

    val values = mapOf("version" to getManifestVersionString(componentsVersion, topobjdir, mozBuildDate))
    task.inputs.properties(values)
    task.expand(values)
}

@Serializable
data class BuildConfig(
    val projects: Map<String, ProjectConfig> = emptyMap(),
    val variants: List<VariantConfig> = emptyList(),
) {
    companion object {
        internal val logger = Logging.getLogger(BuildConfig::class.java)

        fun fromYml(file: File): BuildConfig {
            require(file.exists()) { "Build config file does not exist: ${file.absolutePath}" }
            return Yaml.default.decodeFromString(
                serializer(),
                file.readText(),
            )
        }
    }
}

@Serializable
data class ProjectConfig(
    val path: String? = null,
    val description: String? = null,
    val publish: Boolean? = null,
    val upstream_dependencies: List<String> = emptyList(),
)

@Serializable
data class VariantConfig(
    val name: String,
    val build_type: String,
    val apks: List<ApkConfig> = emptyList(),
)

@Serializable
data class ApkConfig(
    val abi: String,
    val fileName: String,
)

class BuildConfigPlugin : Plugin<Project> {
    override fun apply(project: Project) {
        project.tasks.register("printProjectDependencies") {
            description = "Prints inter-project dependencies as JSON"
            val outputFile = File(project.projectDir, "build/printProjectDependencies.json")

            val allDeps = mutableMapOf<String, List<String>>()
            project.subprojects.forEach { subproject ->
                val name = subproject.path.removePrefix(":")
                val deps = mutableSetOf<String>()

                subproject.configurations
                    .filter { it.isCanBeResolved }
                    .forEach { config ->
                        config.incoming.resolutionResult.allComponents.forEach { component ->
                            val id = component.id
                            if (id is ProjectComponentIdentifier) {
                                val depPath = id.projectPath.removePrefix(":")
                                if (depPath != name) {
                                    deps.add(depPath)
                                }
                            }
                        }
                    }

                allDeps[name] = deps.sorted()
            }

            doLast {
                val json = groovy.json.JsonOutput.toJson(allDeps)
                outputFile.parentFile.mkdirs()
                outputFile.writeText(json)
                logger.debug("Wrote project dependencies to $outputFile")
            }
        }
    }
}

/**
 * Registers projects from a [BuildConfig] into the Gradle [Settings], optionally filtering
 * which projects to include via [shouldIncludeProject].
 *
 * @param settings the Gradle [Settings] instance to register projects into.
 * @param buildConfig the parsed [BuildConfig] containing project definitions.
 * @param baseDir the base directory that project paths in [buildConfig] are relative to.
 * @param shouldIncludeProject predicate to filter projects by name and config.
 */
fun includeProjects(
    settings: Settings,
    buildConfig: BuildConfig,
    baseDir: File,
    shouldIncludeProject: (String, ProjectConfig) -> Boolean = { _, _ -> true },
) {
    val componentsDir = File(baseDir, "components")
    if (componentsDir.exists()) {
        settings.include(":components")
        settings.project(":components").projectDir = componentsDir
    }

    val descriptions = mutableMapOf<String, String>()
    var includedCount = 0

    buildConfig.projects
        .filter { (name, config) -> shouldIncludeProject(name, config) }
        .forEach { (name, config) ->
            settings.include(":$name")

            config.path?.let { path ->
                settings.project(":$name").projectDir = File(baseDir, path)
            }

            config.description?.let { descriptions[":$name"] = it }
            includedCount++
        }

    if (descriptions.isNotEmpty()) {
        settings.gradle.beforeProject(Action {
            descriptions[path]?.let {
                extensions.extraProperties.set("description", it)
            }
        })
    }

    settings.gradle.projectsLoaded(Action {
        rootProject.extensions.extraProperties["buildConfig"] = buildConfig
    })

    BuildConfig.logger.debug("BuildConfig> Loaded $includedCount projects")
}
