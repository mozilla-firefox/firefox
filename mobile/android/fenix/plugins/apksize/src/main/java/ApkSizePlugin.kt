/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import com.android.build.api.artifact.SingleArtifact
import com.android.build.api.variant.AndroidComponentsExtension
import com.android.build.api.variant.BuiltArtifactsLoader
import com.android.build.gradle.AppPlugin
import org.gradle.api.DefaultTask
import org.gradle.api.Plugin
import org.gradle.api.Project
import org.gradle.api.file.DirectoryProperty
import org.gradle.api.provider.Property
import org.gradle.api.tasks.Input
import org.gradle.api.tasks.InputDirectory
import org.gradle.api.tasks.Internal
import org.gradle.api.tasks.Optional
import org.gradle.api.tasks.TaskAction
import org.gradle.kotlin.dsl.withType
import org.json.JSONArray
import org.json.JSONObject
import java.io.File
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.nio.file.Files
import java.util.zip.ZipEntry
import java.util.zip.ZipFile

/**
 * This plugin generates a [ApkSizeTask] for each variant of the target
 * project dependant on the APK output artifacts.
 */
class ApkSizePlugin : Plugin<Project> {
    override fun apply(project: Project) {
        project.plugins.withType<AppPlugin>().configureEach {
            val androidComponents = project.extensions.getByType(AndroidComponentsExtension::class.java)
            androidComponents.onVariants { variant ->
                val taskName = "apkSize${variant.name.replaceFirstChar { it.uppercase() }}"
                val task = project.tasks.register(taskName, ApkSizeTask::class.java)
                task.configure {
                    artifactsDirectory.set(variant.artifacts.get(SingleArtifact.APK))
                    artifactsLoader.set(variant.artifacts.getBuiltArtifactsLoader())
                    mozAutomation.set(project.providers.environmentVariable("MOZ_AUTOMATION"))
                    mozPerfherderUpload.set(project.providers.environmentVariable("MOZ_PERFHERDER_UPLOAD"))
                }
            }
        }
    }
}

/**
 * Gradle task for determining the size of APKs and logging them in a perfherder compatible format.
 */
abstract class ApkSizeTask : DefaultTask() {
    /**
     * Directory containing APKs that get built for the build variant.
     */
    @get:InputDirectory
    abstract val artifactsDirectory: DirectoryProperty

    /**
     * The apk directory has a metadata file indicating relevant files
     * and this loader processes that to give us the all the splits the
     * form the abstract artifact.
     */
    @get:Internal
    abstract val artifactsLoader: Property<BuiltArtifactsLoader>

    @get:Input
    @get:Optional
    abstract val mozAutomation: Property<String>

    @get:Input
    @get:Optional
    abstract val mozPerfherderUpload: Property<String>

    @TaskAction
    fun logApkSize() {
        val builtArtifacts = artifactsLoader.get().load(artifactsDirectory.get())
            ?: throw IllegalStateException("Cannot load APK metadata")

        val variantName = builtArtifacts.variantName.replaceFirstChar { it.uppercase() }
        val apks = builtArtifacts.elements.map { File(it.outputFile) }
        val apkSizes = apks.associate { it.name to Files.size(it.toPath()) }
        // Every apk of a variant contains the same dex files, so just pick the first one.
        val dexStats = determineDexStats(apks.first())
        val json = buildPerfherderJson(variantName, apkSizes, dexStats)

        val isAutomation = mozAutomation.orNull == "1"
        val uploadPath = mozPerfherderUpload.orNull
        if (isAutomation && uploadPath != null) {
            val outputFile = File(uploadPath)
            outputFile.parentFile?.mkdirs()
            outputFile.writeText(json.toString())
        }
    }
}

/**
 * Sizes and class count summed over the `classes*.dex` files of an APK. Each class is defined in
 * exactly one dex file, so the class count is the number of classes in the APK.
 */
internal data class DexStats(
    val uncompressedSize: Long,
    val compressedSize: Long,
    val classCount: Long,
) {
    operator fun plus(other: DexStats) = DexStats(
        uncompressedSize + other.uncompressedSize,
        compressedSize + other.compressedSize,
        classCount + other.classCount,
    )
}

private val DEX_FILE_NAME = Regex("classes\\d*\\.dex")
private val DEX_MAGIC = "dex\n".toByteArray()
private const val DEX_HEADER_SIZE = 0x70
private const val CLASS_DEFS_SIZE_OFFSET = 0x60

internal fun determineDexStats(apk: File): DexStats = ZipFile(apk).use { zip ->
    val dexEntries = zip.entries().asSequence().filter { DEX_FILE_NAME.matches(it.name) }.toList()
    check(dexEntries.isNotEmpty()) { "${apk.name} contains no dex files" }
    dexEntries.map { readDexStats(zip, it) }.reduce(DexStats::plus)
}

private fun readDexStats(zip: ZipFile, entry: ZipEntry): DexStats {
    val header = zip.getInputStream(entry).use { it.readNBytes(DEX_HEADER_SIZE) }
    check(header.size == DEX_HEADER_SIZE && header.copyOf(DEX_MAGIC.size).contentEquals(DEX_MAGIC)) {
        "${entry.name} is not a dex file"
    }
    val buffer = ByteBuffer.wrap(header).order(ByteOrder.LITTLE_ENDIAN)
    return DexStats(
        uncompressedSize = entry.size,
        compressedSize = entry.compressedSize,
        classCount = buffer.getInt(CLASS_DEFS_SIZE_OFFSET).toUInt().toLong(),
    )
}

/**
 * Returns perfherder compatible JSON for tracking the file size of APKs and the size and contents
 * of their dex code.
 *
 * ```
 * {
 *   "framework": {
 *     "name": "build_metrics"
 *   },
 *   "suites": [
 *     {
 *       "name": "apk-size-[debug,nightly,beta,release]",
 *       "lowerIsBetter": true,
 *       "subtests": [
 *         { "name": "app-arm64-v8a-debug.apk", "value": 98855735 },
 *         { "name": "app-armeabi-v7a-debug.apk", "value": 92300031 },
 *         { "name": "app-x86-debug.apk", "value": 103410909 },
 *         { "name": "app-x86_64-debug.apk", "value": 102465675 }
 *       ],
 *       "value":98855735,
 *       "shouldAlert":false
 *     },
 *     {
 *       "name": "dex-size-[debug,nightly,beta,release]",
 *       "lowerIsBetter": true,
 *       "subtests": [
 *         { "name": "compressed-size", "value": 11658374 },
 *         { "name": "uncompressed-size", "value": 27869260 },
 *         { "name": "class-count", "value": 41421 }
 *       ],
 *       "value":11658374,
 *       "shouldAlert":false
 *     }
 *   ]
 * }
 * ```
 */
internal fun buildPerfherderJson(
    variantName: String,
    apkSize: Map<String, Long>,
    dexStats: DexStats,
): JSONObject {
    val data = JSONObject()

    val framework = JSONObject()
    framework.put("name", "build_metrics")
    data.put("framework", framework)

    val suites = JSONArray()

    val suite = JSONObject()
    suite.put("name", "apk-size-$variantName")
    suite.put("value", getSummarySize(apkSize))
    suite.put("lowerIsBetter", true)
    suite.put("alertChangeType", "absolute")
    suite.put("alertThreshold", 1024 * 1024)

    if (variantName.contains("debug", ignoreCase = true)) {
        suite.put("shouldAlert", false)
    }

    val subtests = JSONArray()
    apkSize.forEach { (apk, size) ->
        val subtest = JSONObject()
        subtest.put("name", apk)
        subtest.put("value", size)
        subtests.put(subtest)
    }
    suite.put("subtests", subtests)

    suites.put(suite)
    suites.put(buildDexSuite(variantName, dexStats))

    data.put("suites", suites)

    return data
}

private fun buildDexSuite(variantName: String, dexStats: DexStats): JSONObject {
    val suite = JSONObject()
    suite.put("name", "dex-size-$variantName")
    suite.put("value", dexStats.compressedSize)
    suite.put("unit", "bytes")
    suite.put("lowerIsBetter", true)
    suite.put("alertChangeType", "absolute")
    suite.put("alertThreshold", 50 * 1024)

    if (variantName.contains("debug", ignoreCase = true)) {
        suite.put("shouldAlert", false)
    }

    val subtests = JSONArray()
    listOf(
        Triple("compressed-size", dexStats.compressedSize, "bytes"),
        Triple("uncompressed-size", dexStats.uncompressedSize, "bytes"),
        Triple("class-count", dexStats.classCount, "count"),
    ).forEach { (name, value, unit) ->
        val subtest = JSONObject()
        subtest.put("name", name)
        subtest.put("value", value)
        subtest.put("unit", unit)
        subtest.put("lowerIsBetter", true)
        subtests.put(subtest)
    }
    suite.put("subtests", subtests)

    return suite
}

/**
 * Returns a summarized size for the APKs. This is the main value that is getting tracked. The size
 * of the individual APKs will be reported as "subtests".
 */
private fun getSummarySize(apkSize: Map<String, Long>): Long {
    val arm64size = apkSize.keys.find { it.contains("arm64") }?.let { apk -> apkSize[apk] }
    if (arm64size != null) {
        // If available we will report the size of the arm64 APK as the summary. This is the most
        // important and most installed APK.
        return arm64size
    }

    // If there's no arm64 APK then we calculate a simple average.
    return apkSize.values.sum() / apkSize.size
}
