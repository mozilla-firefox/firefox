/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.conventions

import java.io.File
import org.gradle.api.Plugin
import org.gradle.api.Project
import org.gradle.api.artifacts.VersionCatalogsExtension
import org.gradle.api.file.Directory
import org.gradle.api.file.DuplicatesStrategy
import org.gradle.api.provider.Provider
import org.gradle.api.tasks.ClasspathNormalizer
import org.gradle.api.tasks.Copy
import org.gradle.api.tasks.compile.JavaCompile
import org.jetbrains.kotlin.gradle.tasks.KotlinCompile

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
        configureScipKotlinc(project, topsrcdir, targetdir)
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

    private fun configureScipKotlinc(
        project: Project,
        topsrcdir: String,
        targetdir: File,
    ) {
        val scipKotlincDep = getDependency(project, "scip-kotlinc")
        val scipKotlincConfig = project.configurations.register("scipKotlinc")
        scipKotlincConfig.configure { dependencies.addLater(scipKotlincDep) }

        fun KotlinCompile.getScipTargetDir(): Provider<Directory> = project.layout.buildDirectory.dir("scip/${name}")

        // Each KotlinCompile task configured below outputs its SCIP files to a separate destination directory.
        // This merges them all together into targetdir.
        val collectKotlinScip = project.tasks.register("collectKotlinScip", Copy::class.java) {
            from(project.tasks.withType(KotlinCompile::class.java).map { it.getScipTargetDir() })
            into(targetdir)
            duplicatesStrategy = DuplicatesStrategy.INCLUDE
        }

        project.tasks.withType(KotlinCompile::class.java).configureEach {
            pluginClasspath.from(scipKotlincConfig)

            val scipTargetDir = getScipTargetDir()
            outputs.dir(scipTargetDir)
            compilerOptions.freeCompilerArgs.let {
                it.addAll("-P", "plugin:scip-kotlinc:sourceroot=${topsrcdir}")
                it.add("-P")
                it.add(scipTargetDir.map { "plugin:scip-kotlinc:targetroot=${it}" })
            }

            finalizedBy(collectKotlinScip)
        }
    }

    private fun getDependency(project: Project, name: String) = project.provider {
        val versionCatalogs = project.extensions.getByType(VersionCatalogsExtension::class.java)
        val libs = versionCatalogs.named("libs")
        project.dependencies.create(libs.findLibrary(name).get().get())
    }
}
