/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.androidtestkeeprules

import java.io.File
import java.net.URI
import java.nio.file.FileSystems
import java.nio.file.Files
import java.util.zip.ZipEntry
import java.util.zip.ZipFile
import java.util.zip.ZipOutputStream
import javax.tools.ToolProvider
import org.gradle.testfixtures.ProjectBuilder
import org.junit.jupiter.api.Assertions.assertEquals
import org.junit.jupiter.api.Assertions.assertFalse
import org.junit.jupiter.api.Assertions.assertTrue
import org.junit.jupiter.api.Test
import org.junit.jupiter.api.io.TempDir

class GenerateAndroidTestKeepRulesTest {
    @TempDir lateinit var tempDir: File

    @Test
    fun `keeps the app members that test code references`() {
        val rules = generateRules()

        assertTrue(rules.contains("-keep class app.Used {"), rules)
        assertFalse(rules.contains("allowobfuscation"), rules)
        assertTrue(rules.contains("public void called();"), rules)
        assertFalse(rules.contains("notCalled"), rules)
        assertFalse(rules.contains("app.Unused"), rules)
    }

    @Test
    fun `treats classes the app also contains as app code`() {
        val rules = generateRules()

        assertTrue(rules.contains("class shared.Shared {"), rules)
        assertFalse(rules.contains("app.Hidden"), rules)
    }

    @Test
    fun `writeClassesJar copies class files only, once each, without excluded classes`() {
        val jar = File(tempDir, "input.jar")
        writeJar(
            jar,
            "a/B.class",
            "c/D.class",
            "module-info.class",
            "META-INF/versions/9/a/B.class",
            "resource.txt",
        )
        val dir = File(tempDir, "input-dir")
        listOf("a/B.class", "e/F.class", "e/notes.txt").forEach { path ->
            File(dir, path).apply { parentFile.mkdirs() }.writeText(path)
        }
        val output = File(tempDir, "output.jar")

        val written = writeClassesJar(output, listOf(jar), listOf(dir), exclude = setOf("c/D.class"))

        assertEquals(setOf("a/B.class", "e/F.class"), written)
        assertEquals(written, ZipFile(output).use { zip -> zip.entries().asSequence().map { it.name }.toSet() })
        assertEquals("a/B.class", ZipFile(output).use { zip -> zip.getInputStream(zip.getEntry("a/B.class")).reader().readText() })
    }

    /**
     * Compiles a small app and test code base, splits it the way AGP presents the app and androidTest classes, and
     * returns the rules the task generates for it.
     */
    private fun generateRules(): String {
        val classes = compile(
            "app/Used.java" to "package app; public class Used { public void called() {} public void notCalled() {} }",
            "app/Unused.java" to "package app; public class Unused { public void method() {} }",
            "app/Hidden.java" to "package app; public class Hidden { public static void secret() {} }",
            "shared/Shared.java" to "package shared; public class Shared { public static void used() { app.Hidden.secret(); } }",
            "test/UsesApp.java" to
                "package test; public class UsesApp { void run() { new app.Used().called(); shared.Shared.used(); } }",
        )

        val appJar = File(tempDir, "app.jar")
        writeJarFrom(classes, appJar, "app/", "shared/")
        val testDir = File(tempDir, "test-classes")
        listOf("test/", "shared/").forEach { prefix ->
            classes.walk().filter { it.isFile && it.relativeTo(classes).invariantSeparatorsPath.startsWith(prefix) }
                .forEach { it.copyTo(File(testDir, it.relativeTo(classes).path)) }
        }

        val project = ProjectBuilder.builder().withProjectDir(File(tempDir, "project")).build()
        val task = project.tasks.register("generateKeepRules", GenerateAndroidTestKeepRules::class.java).get()
        task.appJars.add(project.layout.file(project.provider { appJar }))
        task.testDirs.add(project.layout.dir(project.provider { testDir }))
        task.bootClasspath.from(javaLangObjectJar())
        val output = File(tempDir, "rules.pro")
        task.outputFile.set(output)

        task.generate()
        return output.readText()
    }

    /** The fixtures only depend on [Object], so a jar holding it is enough of a platform library. */
    private fun javaLangObjectJar(): File {
        val objectClass = FileSystems.getFileSystem(URI.create("jrt:/")).getPath("modules/java.base/java/lang/Object.class")
        val jar = File(tempDir, "library.jar")
        ZipOutputStream(jar.outputStream()).use { out ->
            out.putNextEntry(ZipEntry("java/lang/Object.class"))
            out.write(Files.readAllBytes(objectClass))
            out.closeEntry()
        }
        return jar
    }

    private fun compile(vararg sources: Pair<String, String>): File {
        val sourceDir = File(tempDir, "src")
        val files = sources.map { (path, code) -> File(sourceDir, path).apply { parentFile.mkdirs() }.apply { writeText(code) } }
        val classesDir = File(tempDir, "classes").apply { mkdirs() }
        val compiler = ToolProvider.getSystemJavaCompiler()
        val result = compiler.run(null, null, null, "-d", classesDir.path, *files.map { it.path }.toTypedArray())
        assertEquals(0, result, "fixture compilation failed")
        return classesDir
    }

    private fun writeJarFrom(classesDir: File, jar: File, vararg prefixes: String) {
        ZipOutputStream(jar.outputStream()).use { out ->
            classesDir.walk().filter { it.isFile }.forEach { file ->
                val name = file.relativeTo(classesDir).invariantSeparatorsPath
                if (prefixes.any { name.startsWith(it) }) {
                    out.putNextEntry(ZipEntry(name))
                    out.write(file.readBytes())
                    out.closeEntry()
                }
            }
        }
    }

    private fun writeJar(jar: File, vararg names: String) {
        ZipOutputStream(jar.outputStream()).use { out ->
            names.forEach { name ->
                out.putNextEntry(ZipEntry(name))
                out.write(name.toByteArray())
                out.closeEntry()
            }
        }
    }
}
