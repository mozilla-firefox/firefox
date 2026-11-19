/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.conventions

import java.io.File
import java.nio.file.Paths
import javax.inject.Inject
import org.gradle.api.DefaultTask
import org.gradle.api.Plugin
import org.gradle.api.Project
import org.gradle.api.artifacts.VersionCatalogsExtension
import org.gradle.api.file.ConfigurableFileCollection
import org.gradle.api.file.Directory
import org.gradle.api.file.DirectoryProperty
import org.gradle.api.file.DuplicatesStrategy
import org.gradle.api.provider.Property
import org.gradle.api.provider.Provider
import org.gradle.api.tasks.Classpath
import org.gradle.api.tasks.ClasspathNormalizer
import org.gradle.api.tasks.CompileClasspath
import org.gradle.api.tasks.Copy
import org.gradle.api.tasks.Input
import org.gradle.api.tasks.InputFiles
import org.gradle.api.tasks.OutputDirectory
import org.gradle.api.tasks.PathSensitive
import org.gradle.api.tasks.PathSensitivity
import org.gradle.api.tasks.TaskAction
import org.gradle.api.tasks.compile.JavaCompile
import org.gradle.workers.WorkAction
import org.gradle.workers.WorkParameters
import org.gradle.workers.WorkerExecutor
import org.jetbrains.kotlin.gradle.tasks.KotlinCompile
import org.scip_code.scip_java.kotlin_analysis.KotlinAnalysisIndexer

interface ScipKotlinAnalysisParameters : WorkParameters {
    val sourceroot: Property<String>
    val destinationDirectory: DirectoryProperty
    val libraries: ConfigurableFileCollection
    val sources: ConfigurableFileCollection
}

abstract class ScipKotlinAnalysisAction : WorkAction<ScipKotlinAnalysisParameters> {
    override fun execute() {
        KotlinAnalysisIndexer(
            sourceroot = Paths.get(parameters.sourceroot.get()),
            targetroot = parameters.destinationDirectory.get().getAsFile().toPath(),
            sourceRoots = parameters.sources.map { it.toPath() },
            classpath = parameters.libraries.map { it.toPath() },
        ).run()
    }
}

abstract class ScipKotlinAnalysisTask : DefaultTask() {
    @get:Inject
    abstract val executor: WorkerExecutor

    @get:Classpath
    abstract val analysisClasspath: ConfigurableFileCollection

    @get:Input
    abstract val sourceroot: Property<String>

    @get:OutputDirectory
    abstract val destinationDirectory: DirectoryProperty

    @get:CompileClasspath
    abstract val libraries: ConfigurableFileCollection

    @get:InputFiles
    @get:PathSensitive(PathSensitivity.RELATIVE)
    abstract val sources: ConfigurableFileCollection

    @TaskAction
    fun run() {
        val task = this

        executor.processIsolation {
            classpath.from(analysisClasspath)
        }.submit(ScipKotlinAnalysisAction::class.java) {
            sourceroot.set(task.sourceroot)
            destinationDirectory.set(task.destinationDirectory)
            libraries.from(task.libraries)
            sources.from(task.sources)
        }
    }
}

class MozsearchPlugin : Plugin<Project> {
    override fun apply(project: Project) {
        val mozconfig = project.gradle.extensions.extraProperties["mozconfig"] as Map<*, *>
        val substs = mozconfig["substs"] as Map<*, *>

        if (!substs["ENABLE_MOZSEARCH_PLUGIN"].isTruthy() && !substs["DOWNLOAD_ALL_GRADLE_DEPENDENCIES"].isTruthy()) {
            return
        }

        val topsrcdir = mozconfig["topsrcdir"] as String
        val topobjdir = mozconfig["topobjdir"] as String

        val targetdir = File(topobjdir, "mozsearch_java_index")

        configureScipJavac(project, topsrcdir, targetdir)
        registerScipKotlinAnalysisTasks(project, topsrcdir, targetdir)
    }

    private fun configureScipJavac(
        project: Project,
        topsrcdir: String,
        targetdir: File,
    ) {
        val scipJavacDep = getDependency(project, "scip-javac")
        val scipJavacConfig = project.configurations.register("scipJavac")
        scipJavacConfig.configure { dependencies.addLater(scipJavacDep) }

        fun JavaCompile.getScipTargetDir(): Provider<Directory> = project.layout.buildDirectory.dir("scip/${name}")

        // Each JavaCompile task configured below outputs its SCIP files to a separate destination directory.
        // This merges them all together into targetdir.
        val collectJavaScip = project.tasks.register("collectJavaScip", Copy::class.java) {
            from(project.tasks.withType(JavaCompile::class.java).map { it.getScipTargetDir() })
            into(targetdir)
            duplicatesStrategy = DuplicatesStrategy.INCLUDE
        }

        project.tasks.withType(JavaCompile::class.java).configureEach {
            val pluginClasspath = project.files(scipJavacConfig)
            inputs.files(pluginClasspath).withNormalizer(ClasspathNormalizer::class.java)

            val scipTargetDir = getScipTargetDir()
            outputs.dir(scipTargetDir)

            doFirst {
                classpath += pluginClasspath
                options.compilerArgs.add("-Xplugin:scip -sourceroot:${topsrcdir} -targetroot:${scipTargetDir.get()}")
            }

            finalizedBy(collectJavaScip)
        }
    }

    private fun registerScipKotlinAnalysisTasks(
        project: Project,
        topsrcdir: String,
        targetdir: File,
    ) {
        val scipKotlinAnalysisDep = getDependency(project, "scip-kotlin-analysis")
        val scipKotlinAnalysisConfig = project.configurations.register("scipKotlinAnalysis")
        scipKotlinAnalysisConfig.configure { dependencies.addLater(scipKotlinAnalysisDep) }

        // Each ScipKotlinAnalysisTask defined below outputs its files to a separate destination directory.
        // The collectKotlinScip task merges them all together.
        val collectKotlinScipTask = project.tasks.register("collectKotlinScip", Copy::class.java) {
            from(project.tasks.withType(ScipKotlinAnalysisTask::class.java).map { it.destinationDirectory.get().getAsFile().toPath() })
            into(targetdir)
            duplicatesStrategy = DuplicatesStrategy.INCLUDE
        }

        // register() can't be called from configureEach, hence the eager withType.
        //noinspection WithTypeWithoutConfigureEach
        project.tasks.withType(KotlinCompile::class.java) {
            val compileTask = this

            val scipTask = project.tasks.register("${name}Scip", ScipKotlinAnalysisTask::class.java) {
                analysisClasspath.from(scipKotlinAnalysisConfig)

                sourceroot.set(topsrcdir)
                destinationDirectory.set(project.layout.buildDirectory.dir("scip/${compileTask.name}"))
                libraries.from(compileTask.libraries)
                sources.from(compileTask.sources + compileTask.javaSources)

                finalizedBy(collectKotlinScipTask)
            }

            finalizedBy(scipTask)
        }
    }

    private fun getDependency(project: Project, name: String) = project.provider {
        val versionCatalogs = project.extensions.getByType(VersionCatalogsExtension::class.java)
        val libs = versionCatalogs.named("libs")
        project.dependencies.create(libs.findLibrary(name).get().get())
    }
}
