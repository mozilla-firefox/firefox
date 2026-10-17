/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.androidtestkeeprules

import com.android.tools.r8.Diagnostic
import com.android.tools.r8.DiagnosticsHandler
import com.android.tools.r8.DiagnosticsLevel
import com.android.tools.r8.diagnostic.MissingDefinitionsDiagnostic
import com.android.tools.r8.tracereferences.TraceReferences
import com.android.tools.r8.tracereferences.TraceReferencesCommand
import com.android.tools.r8.tracereferences.TraceReferencesKeepRules
import java.io.File
import java.util.zip.ZipEntry
import java.util.zip.ZipFile
import java.util.zip.ZipOutputStream
import org.gradle.api.DefaultTask
import org.gradle.api.file.ConfigurableFileCollection
import org.gradle.api.file.Directory
import org.gradle.api.file.RegularFile
import org.gradle.api.file.RegularFileProperty
import org.gradle.api.provider.ListProperty
import org.gradle.api.tasks.CacheableTask
import org.gradle.api.tasks.Classpath
import org.gradle.api.tasks.CompileClasspath
import org.gradle.api.tasks.OutputFile
import org.gradle.api.tasks.TaskAction

/**
 * Runs R8's TraceReferences with the classes only the androidTest APK contains as the source, and the app's
 * unminified classes as the target, and writes keep rules for every app class and member the source references.
 *
 * Classes the app also contains are dropped from the source: the test APK leaves them out and uses the app's copy at
 * runtime, so they have to be traced as part of the target.
 */
@CacheableTask
abstract class GenerateAndroidTestKeepRules : DefaultTask() {
    @get:Classpath abstract val appJars: ListProperty<RegularFile>

    @get:Classpath abstract val appDirs: ListProperty<Directory>

    @get:Classpath abstract val testJars: ListProperty<RegularFile>

    @get:Classpath abstract val testDirs: ListProperty<Directory>

    @get:CompileClasspath abstract val bootClasspath: ConfigurableFileCollection

    @get:OutputFile abstract val outputFile: RegularFileProperty

    @TaskAction
    fun generate() {
        val appJar = File(temporaryDir, "app.jar")
        val appClasses = writeClassesJar(appJar, appJars.get().map { it.asFile }, appDirs.get().map { it.asFile })

        val testOnlyJar = File(temporaryDir, "android-test-only.jar")
        writeClassesJar(testOnlyJar, testJars.get().map { it.asFile }, testDirs.get().map { it.asFile }, appClasses)

        val output = outputFile.get().asFile
        output.parentFile.mkdirs()

        val diagnostics = LoggingDiagnosticsHandler()
        TraceReferences.run(
            TraceReferencesCommand.builder(diagnostics)
                .addLibraryFiles(bootClasspath.files.map { it.toPath() })
                .addTargetFiles(appJar.toPath())
                .addSourceFiles(testOnlyJar.toPath())
                .setConsumer(
                    TraceReferencesKeepRules.builder()
                        .setAllowObfuscation(true)
                        .setOutputPath(output.toPath())
                        .build(),
                )
                .build(),
        )

        if (diagnostics.warningCount > 0) {
            logger.lifecycle(
                "${diagnostics.warningCount} TraceReferences warnings while generating ${output.name}, " +
                    "run with --info to see them.",
            )
        }
    }

    /**
     * Reports missing definitions as warnings rather than errors: test-only code can reference optional classes that
     * neither the app nor the platform provides, and those can't need keep rules. Other errors still fail the task.
     */
    private inner class LoggingDiagnosticsHandler : DiagnosticsHandler {
        var warningCount = 0

        override fun modifyDiagnosticsLevel(level: DiagnosticsLevel, diagnostic: Diagnostic) =
            if (diagnostic is MissingDefinitionsDiagnostic) DiagnosticsLevel.WARNING else level

        override fun warning(warning: Diagnostic) {
            warningCount++
            logger.info("TraceReferences: ${warning.diagnosticMessage}")
        }

        override fun info(info: Diagnostic) {
            logger.info("TraceReferences: ${info.diagnosticMessage}")
        }
    }
}

/**
 * Copies every class file from [jars] and [dirs] into [destination], skipping classes in [exclude] and any later
 * duplicates of a class.
 *
 * @return the paths of the classes written, relative to the jar root.
 */
internal fun writeClassesJar(
    destination: File,
    jars: List<File>,
    dirs: List<File>,
    exclude: Set<String> = emptySet(),
): Set<String> {
    val written = HashSet<String>()
    ZipOutputStream(destination.outputStream().buffered()).use { out ->
        fun add(name: String, bytes: () -> ByteArray) {
            if (isClassEntry(name) && name !in exclude && written.add(name)) {
                out.putNextEntry(ZipEntry(name))
                out.write(bytes())
                out.closeEntry()
            }
        }

        jars.filter { it.isFile }.forEach { jar ->
            ZipFile(jar).use { zip ->
                zip.entries().asSequence().filterNot { it.isDirectory }.forEach { entry ->
                    add(entry.name) { zip.getInputStream(entry).use { it.readBytes() } }
                }
            }
        }
        dirs.filter { it.isDirectory }.forEach { dir ->
            dir.walk().filter { it.isFile }.forEach { file ->
                add(file.relativeTo(dir).invariantSeparatorsPath) { file.readBytes() }
            }
        }
    }
    return written
}

private fun isClassEntry(name: String) =
    name.endsWith(".class") && !name.startsWith("META-INF/") && name != "module-info.class"
