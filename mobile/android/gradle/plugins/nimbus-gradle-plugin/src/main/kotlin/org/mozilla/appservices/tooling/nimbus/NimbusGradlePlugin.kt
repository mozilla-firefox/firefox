/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.appservices.tooling.nimbus

import com.android.build.api.variant.AndroidComponentsExtension
import org.gradle.api.GradleException
import org.gradle.api.Plugin
import org.gradle.api.Project
import org.gradle.api.artifacts.type.ArtifactTypeDefinition
import org.gradle.api.provider.ListProperty
import org.gradle.api.provider.MapProperty
import org.gradle.api.provider.Property
import org.gradle.api.provider.Provider
import org.mozilla.conventions.isTruthy
import java.io.File

// Not in the org.mozilla.appservices group, which the conventions plugin substitutes with in-tree
// or locally published application-services modules.
private const val FML_GROUP = "org.mozilla.appservices.tools"
private const val FML_MODULE = "nimbus-fml"
private const val FML_BINARY_TYPE = "nimbus-fml-binary"

abstract class NimbusPluginExtension {
    /**
     * The .fml.yaml manifest file.
     *
     * If absent this defaults to `nimbus.fml.yaml`.
     * If relative, it is relative to the project root.
     */
    abstract val manifestFile: Property<String>

    /**
     * The .fml.yaml files that `manifestFile` loads transitively using `includes` or `import`. Changing one of them
     * regenerates the code.
     *
     * Paths are relative to the project root. This defaults to an empty list.
     */
    abstract val manifestDependencies: ListProperty<String>

    /**
     * The mapping between the build variant and the release channel.
     *
     * Variants that are not in this map are used literally.
     */
    abstract val channels: MapProperty<String, String>
}

class NimbusPlugin : Plugin<Project> {
    override fun apply(project: Project) {
        val extension = project.extensions.create("nimbus", NimbusPluginExtension::class.java)
        extension.manifestFile.convention("nimbus.fml.yaml")

        // We need to locate our nimbus-fml tool: with application-services in tree, `./mach build`
        // builds it; otherwise it comes from the application-services archive, resolved like any
        // other dependency.
        val mozconfig = project.gradle.extensions.extraProperties["mozconfig"] as Map<*, *>
        val topsrcdir = mozconfig["topsrcdir"] as String
        val mozconfigSubsts = mozconfig["substs"] as Map<*, *>

        if (mozconfigSubsts["MOZ_APPSERVICES_IN_TREE"].isTruthy()) {
            // Unset in android-gradle-dependencies builds.
            val nimbusFml = (mozconfigSubsts["NIMBUS_FML"] as? String)?.let(::File)
            setupAndroidVariants(project, extension, project.providers.provider { nimbusFml })
        } else {
            addFmlRepositories(project, (mozconfigSubsts["NIMBUS_FML_REPOSITORY"] as? String)?.let { File(topsrcdir).resolve(it) })

            val fmlArchive = project.configurations.create("nimbusFml") {
                isCanBeConsumed = false
            }
            project.dependencies.add(fmlArchive.name, "$FML_GROUP:$FML_MODULE:${fmlVersion(project, topsrcdir)}@zip")

            project.dependencies.registerTransform(NimbusFmlBinaryTransform::class.java) {
                from.attribute(ArtifactTypeDefinition.ARTIFACT_TYPE_ATTRIBUTE, ArtifactTypeDefinition.ZIP_TYPE)
                to.attribute(ArtifactTypeDefinition.ARTIFACT_TYPE_ATTRIBUTE, FML_BINARY_TYPE)
                parameters.platform.set(NimbusFmlBinaryTransform.detectPlatform(project.providers))
            }
            val fmlBinaryFile = fmlArchive.incoming.artifactView {
                attributes.attribute(ArtifactTypeDefinition.ARTIFACT_TYPE_ATTRIBUTE, FML_BINARY_TYPE)
            }.files.elements.map { it.single().asFile }
            setupAndroidVariants(project, extension, fmlBinaryFile)
        }
    }

    private fun setupAndroidVariants(project: Project, extension: NimbusPluginExtension, fmlBinaryFile: Provider<File>) {
        project.pluginManager.withPlugin("com.android.base") {
            project.extensions.getByType(AndroidComponentsExtension::class.java).onVariants { variant ->
                val taskSuffix = variant.name.replaceFirstChar { it.uppercase() }
                val generateTask = project.tasks.register("nimbusFeatures$taskSuffix", NimbusFeaturesTask::class.java) {
                    description = "Generate Kotlin data classes for Nimbus enabled features"
                    group = "Nimbus"
                    applicationServicesDir.set(project.gradle.extensions.extraProperties.properties["localProperties.autoPublish.application-services.dir"] as? String)
                    if (applicationServicesDir.isPresent) {
                        doNotTrackState("nimbus-fml is built from an application-services checkout Gradle doesn't track")
                    } else {
                        fmlBinary.fileProvider(fmlBinaryFile)
                    }
                    inputFile.set(project.layout.projectDirectory.file(extension.manifestFile))
                    manifestDependencies.setFrom(project.files(extension.manifestDependencies))
                    channel.set(extension.channels.getting(variant.name).orElse(variant.name))
                    // Each task gets its own cache subdirectory because Gradle discourages
                    // "overlapping outputs" which inhibit caching and parallelization
                    // (https://github.com/gradle/gradle/issues/28394).
                    cacheDir.set(project.layout.buildDirectory.dir("nimbus-cache/features$taskSuffix"))
                    outputDir.set(project.layout.buildDirectory.dir("generated/source/nimbus/${variant.name}/kotlin"))
                }
                variant.sources.java?.addGeneratedSourceDirectory(generateTask, NimbusFeaturesTask::outputDir)
            }
        }
    }

    // Releases are on archive.mozilla.org and nightlies in the Taskcluster index; automation
    // provides the archive in a local repository with the same layout as archive.mozilla.org.
    private fun addFmlRepositories(project: Project, localRepository: File?) {
        fun fmlRepository(url: Any, pattern: String) = project.repositories.ivy {
            setUrl(url)
            patternLayout { artifact(pattern) }
            metadataSources { artifact() }
        }

        val releasePattern = "[revision]/[artifact].[ext]"
        project.repositories.exclusiveContent {
            forRepositories(
                *listOfNotNull(
                    localRepository?.let { fmlRepository(it, releasePattern) },
                    fmlRepository("https://archive.mozilla.org/pub/app-services/releases/", releasePattern),
                    fmlRepository(
                        "https://firefox-ci-tc.services.mozilla.com/api/index/v1/task/",
                        "project.application-services.v2.nimbus-fml.[revision]/artifacts/public/build/[artifact].[ext]"
                    ),
                ).toTypedArray()
            )
            filter { includeModule(FML_GROUP, FML_MODULE) }
        }
    }

    private fun fmlVersion(project: Project, topsrcdir: String): String {
        (project.gradle.extensions.extraProperties.properties["localProperties.branchBuild.application-services.version"] as? String)?.let { return it }
        val appServicesFile = File(topsrcdir, "mobile/android/android-components/plugins/dependencies/src/main/java/ApplicationServices.kt")
        if (!appServicesFile.isFile) {
            throw GradleException("Could not determine application-services version: $appServicesFile does not exist")
        }
        return Regex("""val VERSION = "([^"]+)"""").find(appServicesFile.readText())?.groupValues?.get(1)
            ?: throw GradleException("Could not determine application-services version from $appServicesFile")
    }
}
