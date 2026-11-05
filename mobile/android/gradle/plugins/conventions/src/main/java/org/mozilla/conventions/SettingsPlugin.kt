/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.conventions

import org.gradle.api.GradleException
import org.gradle.api.Plugin
import org.gradle.api.flow.FlowProviders
import org.gradle.api.flow.FlowScope
import org.gradle.api.initialization.Settings
import org.gradle.api.logging.Logging
import org.gradle.api.tasks.testing.Test
import org.gradle.build.event.BuildEventsListenerRegistry
import org.gradle.kotlin.dsl.always
import org.gradle.kotlin.dsl.create
import java.io.File
import java.util.UUID
import javax.inject.Inject

private data class AutoPublishConfig(
    val propertyName: String,
    val displayName: String,
    val publishScript: String
)

// Windows can't execute .py files directly, so we assume a "manually installed" python,
// which comes with a "py" launcher and respects the shebang line to specify the version.
private val isWindows = System.getProperty("os.name").lowercase().contains("windows")

abstract class SettingsPlugin : Plugin<Settings> {
    private val logger = Logging.getLogger(SettingsPlugin::class.java)

    @get:Inject
    protected abstract val buildEventsListenerRegistry: BuildEventsListenerRegistry

    @get:Inject
    protected abstract val flowScope: FlowScope

    @get:Inject
    protected abstract val flowProviders: FlowProviders

    override fun apply(settings: Settings) {
        val configStartMs = System.currentTimeMillis()
        val extension = settings.extensions.create<SettingsExtension>("mozilla")
        extension.disableAndroidComponentsTasks.convention(
            settings.startParameter.projectProperties["disableAndroidComponentsTasks"]?.toBoolean() ?: false
        )

        loadBuildConfig(settings)

        settings.gradle.settingsEvaluated {
            configureAutoPublish(settings)
        }

        settings.gradle.allprojects {
            pluginManager.apply(ProjectPlugin::class.java)
            extensions.configure(ProjectExtension::class.java) {
                androidComponentsProject.set(
                    projectDir.absolutePath.replace('\\', '/').contains("/android-components/")
                )
            }
        }

        settings.gradle.rootProject {
            pluginManager.apply(BuildConfigPlugin::class.java)
        }

        configureAcTestAndLintDisabling(settings, extension)
        initializeBuildMetrics(settings, configStartMs)
    }

    private fun loadBuildConfig(settings: Settings) {
        val mozconfig = settings.gradle.extensions.extraProperties["mozconfig"] as Map<*, *>
        val topsrcdir = mozconfig["topsrcdir"] as String
        val rootDir = settings.rootDir.absolutePath

        val buildConfigFile = File(topsrcdir, "mobile/android/android-components/.buildconfig.yml")
        val baseDir = File(topsrcdir, "mobile/android/android-components")
        val buildConfig = BuildConfig.fromYml(buildConfigFile)

        // This could be improved by adding a `sample` flag (or similar) to
        // .buildconfig.yml so inclusion is driven by project metadata rather than
        // name-prefix matching.
        val isAndroidLint = settings.startParameter.projectProperties.containsKey("android-lint")
        val shouldIncludeProject = { name: String, _: ProjectConfig ->
            isAndroidLint || rootDir.contains("android-components") || !name.startsWith("components:samples")
        }

        includeProjects(settings, buildConfig, baseDir, shouldIncludeProject)
    }

    // Configures automatic publication of local Application Services and/or Glean
    // to the Maven local repository.
    //
    // For convenience, this reads the `autoPublish.*` properties from
    // `$topsrcdir/local.properties`, so that you only need to set them once
    // for all Android projects.
    //
    // You can also set or override these properties on a per-project basis,
    // by setting them in `$topsrcdir/mobile/android/{project}/local.properties`,
    // if you want to only substitute App Services or Glean for a specific project,
    // or to substitute different versions for different projects.
    private fun configureAutoPublish(settings: Settings) {
        val extraProperties = settings.gradle.extensions.extraProperties

        AUTO_PUBLISH_CONFIGS.forEach { config ->
            val propertyKey = "localProperties.autoPublish.${config.propertyName}.dir"
            if (extraProperties.has(propertyKey)) {
                val localPath = extraProperties[propertyKey] as String
                logger.lifecycle("SettingsPlugin> Enabling automatic publication of ${config.displayName} from: $localPath")
                val publishCmd = buildPythonCommand(config.publishScript)
                runCmd(settings, publishCmd, localPath, "Published ${config.displayName} for local development.")
            } else {
                logger.lifecycle("SettingsPlugin> Disabled auto-publication of ${config.displayName}. Enable it by settings 'autoPublish.${config.propertyName}.dir' in local.properties")
            }
        }
    }

    private fun configureAcTestAndLintDisabling(settings: Settings, extension: SettingsExtension) {
        val rootGradle = generateSequence(settings.gradle) { it.parent }.last()

        rootGradle.taskGraph.whenReady {
            if (!extension.disableAndroidComponentsTasks.get()) {
                return@whenReady
            }

            rootGradle.taskGraph.allTasks.forEach { task ->
                val mozilla = task.project.extensions.findByType(ProjectExtension::class.java)
                    ?: return@forEach
                if (mozilla.androidComponentsProject.get() && (task is Test || task.name.contains("lint"))) {
                    task.project.logger.debug("Disabling task ${task.path}")
                    task.enabled = false
                }
            }
        }
    }

    private fun initializeBuildMetrics(settings: Settings, configStartMs: Long) {
        val rootGradle = generateSequence(settings.gradle) { it.parent }.last()
        val outputDir = rootGradle.startParameter.projectProperties["buildMetricsOutputDir"] ?: return
        val buildId = UUID.randomUUID().toString()

        ConfiguredBuilds.recordConfigStart(buildId)
        rootGradle.taskGraph.whenReady {
            ConfiguredBuilds.recordConfigEnd(buildId, System.currentTimeMillis())
        }

        // Register a task listener for all builds
        val buildMetricsProvider = rootGradle.sharedServices.registerIfAbsent(
            "buildMetricsService",
            BuildMetricsService::class.java
        ) {
            parameters.outputDir.set(outputDir)
            rootGradle.startParameter.projectProperties["buildMetricsFileSuffix"]?.let { parameters.fileSuffix.set(it) }
            parameters.buildId.set(buildId)
            parameters.configStartMs.set(configStartMs)
        }

        buildEventsListenerRegistry.onTaskCompletion(buildMetricsProvider)

        val perfherderOptions = rootGradle.startParameter.projectProperties["buildMetricsPerfherderOptions"] ?: return
        val instanceType = settings.providers.environmentVariable("TASKCLUSTER_INSTANCE_TYPE").orNull
        flowScope.always(ReportBuildTimes::class) {
            parameters.service.set(buildMetricsProvider)
            parameters.failed.set(flowProviders.buildWorkResult.map { it.failure.isPresent })
            parameters.extraOptions.set(
                perfherderOptions.split(" ").filter { it.isNotBlank() } +
                    listOfNotNull(instanceType?.let { "taskcluster-$it" })
            )
            parameters.outputDir.set(outputDir)
        }
    }

    private fun buildPythonCommand(script: String): List<String> = buildList {
        if (isWindows) {
            add("py")
        }
        add(script)
    }

    private fun runCmd(settings: Settings, cmd: List<String>, workingDirectory: String, successMessage: String) {
        val proc = settings.providers.exec {
            commandLine(cmd)
            isIgnoreExitValue = true
            workingDir = File(workingDirectory)
        }

        val result = proc.result.get().exitValue

        if (result != 0) {
            val message = "Process '$cmd' finished with non-zero exit value $result"
            logger.error(message)
            proc.standardOutput.asText.get().lines().forEach { logger.error("> $it") }
            proc.standardError.asText.get().lines().forEach { logger.error("> $it") }
            throw GradleException(message)
        } else {
            logger.lifecycle("SettingsPlugin> $successMessage")
        }
    }

    companion object {
        private val AUTO_PUBLISH_CONFIGS = listOf(
            AutoPublishConfig(
                propertyName = "application-services",
                displayName = "application-services",
                publishScript = "./automation/publish_to_maven_local_if_modified.py"
            ),
            AutoPublishConfig(
                propertyName = "glean",
                displayName = "Glean",
                publishScript = "./build-scripts/publish_to_maven_local_if_modified.py"
            )
        )
    }
}
