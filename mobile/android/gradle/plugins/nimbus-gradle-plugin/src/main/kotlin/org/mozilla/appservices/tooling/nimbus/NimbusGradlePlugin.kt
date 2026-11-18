/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.appservices.tooling.nimbus

import com.android.build.api.variant.AndroidComponentsExtension
import com.android.build.gradle.AppPlugin
import com.android.build.gradle.LibraryPlugin
import org.gradle.api.GradleException
import org.gradle.api.Plugin
import org.gradle.api.Project
import org.gradle.api.artifacts.type.ArtifactTypeDefinition
import org.gradle.api.provider.ListProperty
import org.gradle.api.provider.MapProperty
import org.gradle.api.provider.Property
import org.gradle.api.provider.Provider
import org.gradle.api.provider.ValueSource
import org.gradle.api.provider.ValueSourceParameters
import org.gradle.api.tasks.TaskProvider
import org.mozilla.conventions.isTruthy
import java.io.File

// Not in the org.mozilla.appservices group, which the conventions plugin substitutes with in-tree
// or locally published application-services modules.
private const val FML_GROUP = "org.mozilla.appservices.tools"
private const val FML_MODULE = "nimbus-fml"
private const val FML_BINARY_TYPE = "nimbus-fml-binary"

abstract class ApplicationServicesVersionSource : ValueSource<String, ApplicationServicesVersionSource.Parameters> {
    interface Parameters : ValueSourceParameters {
        val topsrcdir: Property<String>
        val localPropertiesVersion: Property<String>
    }

    override fun obtain(): String {
        // Check for override from local properties first
        val localVersion = parameters.localPropertiesVersion.orNull
        if (localVersion != null) {
            return localVersion
        }
        // Extract from a generated .kt file
        val topsrcdir = parameters.topsrcdir.get()
        val appServicesFile = File(topsrcdir, "mobile/android/android-components/plugins/dependencies/src/main/java/ApplicationServices.kt")
        val content = appServicesFile.readText()

        // Extract version from: val VERSION = "143.20250816050436"
        val versionRegex = Regex("""val VERSION = "([^"]+)"""")
        val matchResult = versionRegex.find(content)
            ?: throw GradleException("Could not determine application-services version from ${appServicesFile.absolutePath}")
        return matchResult.groupValues[1]
    }
}

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

    /**
     * The file(s) containing the version(s)/ref(s)/location(s) for additional repositories.
     *
     * This defaults to an empty list.
     */
    abstract val repoFiles: ListProperty<String>

    /**
     * The directory where downloaded files are or where they should be cached.
     *
     * If missing, this defaults to the Nimbus cache folder in the build directory.
     */
    abstract val cacheDir: Property<String>

    /**
     * The directory where a local installation of application services can be found.
     *
     * This defaults to `null`, in which case the plugin uses the nimbus-fml binary built by
     * `./mach build`, or the one from the application-services archive for this version.
     */
    abstract val applicationServicesDir: Property<String>
}

class NimbusPlugin : Plugin<Project> {
    override fun apply(project: Project) {
        val extension = project.extensions.create("nimbus", NimbusPluginExtension::class.java)

        // Configure default values ("conventions") for our
        // extension properties.
        extension.manifestFile.convention("nimbus.fml.yaml")
        extension.cacheDir.convention("nimbus-cache")

        // We need to locate our nimbus-fml tool: with application-services in tree, `./mach build`
        // builds it; otherwise it comes from the application-services archive, resolved like any
        // other dependency.
        val mozconfig = project.gradle.extensions.extraProperties["mozconfig"] as Map<*, *>
        val topsrcdir = mozconfig["topsrcdir"] as String
        val mozconfigSubsts = mozconfig["substs"] as Map<*, *>

        if (mozconfigSubsts["MOZ_APPSERVICES_IN_TREE"].isTruthy()) {
            // This is subtle.  We capture `NIMBUS_FML` in the configuration cache as a `String`.
            // If we access `project.gradle...` in the `provider` `Callable` below, we capture the
            // `Project` in the configuration cache, which is not desirable.
            //
            // We can't produce a `File` immediately, because in some configurations, namely
            // `android-gradle-dependencies` tasks, `NIMBUS_FML` is legitimately unset (`null`).  So
            // we pass strings around and map to `File` types lazily "by hand".
            //
            // Finally: if this process fails, including with an exception, the framework swallows
            // the details and says something like `MissingValueException`, which can be hard to
            // interpret.  Hence, this explanation of the details.
            val nimbusFmlPath = mozconfigSubsts["NIMBUS_FML"] as? String

            val fmlBinaryString = project.providers.provider {
                nimbusFmlPath
            }

            setupAndroidVariants(project) { generateTask ->
                generateTask.configure {
                    fmlBinary.set(project.layout.file(fmlBinaryString.map { s -> File(s) }))
                }
            }
        } else {
            // building from an app-services artifact.
            addFmlRepositories(project, (mozconfigSubsts["NIMBUS_FML_REPOSITORY"] as? String)?.let { File(topsrcdir).resolve(it) })

            val fmlArchive = project.configurations.create("nimbusFml") {
                isCanBeConsumed = false
            }
            project.dependencies.addProvider(
                fmlArchive.name,
                getProjectVersionProvider(project, topsrcdir).map { "$FML_GROUP:$FML_MODULE:$it@zip" }
            )

            project.dependencies.registerTransform(NimbusFmlBinaryTransform::class.java) {
                from.attribute(ArtifactTypeDefinition.ARTIFACT_TYPE_ATTRIBUTE, ArtifactTypeDefinition.ZIP_TYPE)
                to.attribute(ArtifactTypeDefinition.ARTIFACT_TYPE_ATTRIBUTE, FML_BINARY_TYPE)
                parameters.platform.set(NimbusFmlBinaryTransform.detectPlatform(project.providers))
            }
            val fmlBinaryFile = fmlArchive.incoming.artifactView {
                attributes.attribute(ArtifactTypeDefinition.ARTIFACT_TYPE_ATTRIBUTE, FML_BINARY_TYPE)
            }.files.elements.map { it.single().asFile }

            setupAndroidVariants(project) { generateTask ->
                generateTask.configure {
                    if (!applicationServicesDir.isPresent) {
                        fmlBinary.fileProvider(fmlBinaryFile)
                    }
                }
            }
        }
    }

    private fun setupAndroidVariants(
        project: Project,
        configureGenerateTask: (TaskProvider<NimbusFeaturesTask>) -> Unit
    ) {
        project.plugins.withType(AppPlugin::class.java).configureEach {
            val androidComponents = project.extensions.getByType(AndroidComponentsExtension::class.java)
            setupVariantsForComponents(androidComponents, project, configureGenerateTask)
        }

        project.plugins.withType(LibraryPlugin::class.java).configureEach {
            val androidComponents = project.extensions.getByType(AndroidComponentsExtension::class.java)
            setupVariantsForComponents(androidComponents, project, configureGenerateTask)
        }
    }

    private fun setupVariantsForComponents(
        androidComponents: AndroidComponentsExtension<*, *, *>,
        project: Project,
        configureGenerateTask: (TaskProvider<NimbusFeaturesTask>) -> Unit
    ) {
        androidComponents.onVariants(androidComponents.selector().all()) { variant ->
            val generateTask = project.tasks.register(
                "nimbusFeatures${variant.name.replaceFirstChar { it.uppercase() }}",
                NimbusFeaturesTask::class.java
            ) {
                description = "Generate Kotlin data classes for Nimbus enabled features"
                group = "Nimbus"

                doFirst {
                    logger.info("Nimbus FML generating Kotlin")
                    logger.info("manifest             {}", inputFile.get().asFile)
                    logger.info("cache dir            {}", cacheDir.get().asFile)
                    logger.info("repo file(s)         {}", repoFiles.files.joinToString())
                    logger.info("channel              {}", channel.get())
                }

                doLast {
                    logger.info("outputFile    {}", outputDir.get().asFile)
                }

                configureCommonTaskProperties(this, project, "features${variant.name.replaceFirstChar { it.uppercase() }}")
                val extension = project.extensions.getByType(NimbusPluginExtension::class.java)
                manifestDependencies.setFrom(project.files(extension.manifestDependencies))
                channel.set(extension.channels.getting(variant.name).orElse(variant.name))
                outputDir.set(project.layout.buildDirectory.dir("generated/source/nimbus/${variant.name}/kotlin"))
            }
            configureGenerateTask(generateTask)
            variant.sources.java?.addGeneratedSourceDirectory(generateTask, NimbusFeaturesTask::outputDir)
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

    private fun getProjectVersionProvider(project: Project, topsrcdir: String): Provider<String> {
        val localPropertiesVersion = if (project.gradle.extensions.extraProperties.has("localProperties.branchBuild.application-services.version")) {
            project.gradle.extensions.extraProperties["localProperties.branchBuild.application-services.version"] as? String
        } else {
            null
        }

        return project.providers.of(ApplicationServicesVersionSource::class.java) {
            parameters.topsrcdir.set(topsrcdir)
            parameters.localPropertiesVersion.set(localPropertiesVersion)
        }
    }

    private fun configureCommonTaskProperties(task: NimbusTask, project: Project, cacheDirSuffix: String) {
        val extension = project.extensions.getByType(NimbusPluginExtension::class.java)

        task.repoFiles.setFrom(project.files(extension.repoFiles))
        task.applicationServicesDir.set(extension.applicationServicesDir)
        task.inputFile.set(project.layout.projectDirectory.file(extension.manifestFile))
        // Each task gets its own cache subdirectory because Gradle discourages
        // "overlapping outputs" which inhibit caching and parallelization
        // (https://github.com/gradle/gradle/issues/28394).
        task.cacheDir.set(project.layout.buildDirectory.dir(extension.cacheDir).map {
            it.dir(cacheDirSuffix)
        })
    }
}
