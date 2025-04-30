/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.lib.crash.service

import android.content.Context
import android.os.SystemClock
import androidx.annotation.VisibleForTesting
import kotlinx.serialization.ExperimentalSerializationApi
import kotlinx.serialization.SerialName
import kotlinx.serialization.Serializable
import kotlinx.serialization.SerializationException
import kotlinx.serialization.json.DecodeSequenceMode
import kotlinx.serialization.json.Json
import kotlinx.serialization.json.JsonArray
import kotlinx.serialization.json.JsonElement
import kotlinx.serialization.json.JsonNull
import kotlinx.serialization.json.JsonObject
import kotlinx.serialization.json.JsonPrimitive
import kotlinx.serialization.json.decodeFromJsonElement
import kotlinx.serialization.json.decodeFromStream
import kotlinx.serialization.json.decodeToSequence
import kotlinx.serialization.json.encodeToStream
import kotlinx.serialization.json.jsonArray
import kotlinx.serialization.json.jsonObject
import kotlinx.serialization.json.jsonPrimitive
import kotlinx.serialization.json.long
import mozilla.components.lib.crash.Crash
import mozilla.components.lib.crash.GleanMetrics.Crash.AsyncShutdownTimeoutObject
import mozilla.components.lib.crash.GleanMetrics.Crash.QuotaManagerShutdownTimeoutObject
import mozilla.components.lib.crash.GleanMetrics.Crash.StackTracesObject
import mozilla.components.lib.crash.GleanMetrics.CrashMetrics
import mozilla.components.lib.crash.GleanMetrics.Pings
import mozilla.components.lib.crash.MinidumpAnalyzer
import mozilla.components.lib.crash.db.Breadcrumb
import mozilla.components.lib.crash.db.toBreadcrumb
import mozilla.components.support.base.log.logger.Logger
import mozilla.components.support.ktx.android.content.isMainProcess
import mozilla.telemetry.glean.private.BooleanMetricType
import mozilla.telemetry.glean.private.ObjectMetricType
import mozilla.telemetry.glean.private.QuantityMetricType
import mozilla.telemetry.glean.private.StringMetricType
import java.io.File
import java.io.FileOutputStream
import java.io.IOException
import java.security.MessageDigest
import java.util.Date
import mozilla.components.lib.crash.GleanMetrics.Crash as GleanCrash
import mozilla.components.lib.crash.GleanMetrics.Environment as GleanEnvironment
import mozilla.components.lib.crash.GleanMetrics.Memory as GleanMemory

private val logger = Logger("glean/GleanCrashReporterService")

/**
 * A [CrashReporterService] implementation for recording metrics with Glean.  The purpose of this
 * crash reporter is to collect crash count metrics by capturing [Crash.UncaughtExceptionCrash],
 * [Throwable] and [Crash.NativeCodeCrash] events and record to the respective
 * [mozilla.telemetry.glean.private.CounterMetricType].
 */
class GleanCrashReporterService(
    val context: Context,
    @get:VisibleForTesting(otherwise = VisibleForTesting.PRIVATE)
    internal val file: File = File(context.applicationInfo.dataDir, CRASH_FILE_NAME),
) : CrashTelemetryService {
    companion object {
        // This file is stored in the application's data directory, so it should be located in the
        // same location as the application.
        // The format of this file is simple and uses the keys named below, one per line, to record
        // crashes.  That format allows for multiple crashes to be appended to the file if, for some
        // reason, the application cannot run and record them.
        const val CRASH_FILE_NAME = "glean_crash_counts"

        // These keys correspond to the labels found for crashCount metric in metrics.yaml as well
        // as the persisted crashes in the crash count file (see above comment)
        const val UNCAUGHT_EXCEPTION_KEY = "uncaught_exception"
        const val CAUGHT_EXCEPTION_KEY = "caught_exception"
        const val MAIN_PROCESS_NATIVE_CODE_CRASH_KEY = "main_proc_native_code_crash"
        const val FOREGROUND_CHILD_PROCESS_NATIVE_CODE_CRASH_KEY = "fg_proc_native_code_crash"
        const val BACKGROUND_CHILD_PROCESS_NATIVE_CODE_CRASH_KEY = "bg_proc_native_code_crash"

        private const val MINIDUMP_READ_BUFFER_SIZE: Int = 8192
    }

    /**
     * The subclasses of GleanCrashAction are used to persist Glean actions to handle them later
     * (in the application which has Glean initialized). They are serialized to JSON objects and
     * appended to a file, in case multiple crashes occur prior to being able to submit the metrics
     * to Glean.
     */
    @VisibleForTesting(otherwise = VisibleForTesting.PRIVATE)
    @Serializable
    internal sealed class GleanCrashAction {
        /**
         * Submit the glean metrics/pings.
         */
        abstract fun submit()

        @Serializable
        @SerialName("count")
        data class Count(val label: String) : GleanCrashAction() {
            override fun submit() {
                CrashMetrics.crashCount[label].add()
            }
        }

        @Serializable
        sealed class PingCause {
            abstract fun setMetrics()

            @Serializable
            @SerialName("os_fault")
            data class OsFault(
                val remoteType: String?,
                val extras: JsonObject?,
                val minidumpHash: String?,
            ) : PingCause() {
                override fun setMetrics() {
                    GleanCrash.cause.set("os_fault")
                    remoteType?.let { GleanCrash.remoteType.set(it) }
                    minidumpHash?.let { GleanCrash.minidumpSha256Hash.set(it) }

                    extras?.let(::setExtraMetrics)
                }

                private fun setExtraMetrics(extras: JsonObject) {
                    // We ignore RemoteType and UptimeTS from extras as we have that information in
                    // the Crash object or tracked manually (respectively).

                    GleanCrash.appChannel.setIfNonNull(extras["ReleaseChannel"])
                    GleanCrash.appDisplayVersion.setIfNonNull(extras["Version"])
                    GleanCrash.appBuild.setIfNonNull(extras["BuildID"])
                    GleanCrash.asyncShutdownTimeout.setAsyncShutdownTimeoutIfNonNull(extras["AsyncShutdownTimeout"])
                    GleanCrash.backgroundTaskName.setIfNonNull(extras["BackgroundTaskName"])
                    GleanCrash.eventLoopNestingLevel.setIfNonNull(extras["EventLoopNestingLevel"])
                    GleanCrash.fontName.setIfNonNull(extras["FontName"])
                    GleanCrash.gpuProcessLaunch.setIfNonNull(extras["GPUProcessLaunchCount"])
                    GleanCrash.ipcChannelError.setIfNonNull(extras["ipc_channel_error"])
                    GleanCrash.isGarbageCollecting.setIfNonNull(extras["IsGarbageCollecting"])
                    GleanCrash.mainThreadRunnableName.setIfNonNull(extras["MainThreadRunnableName"])
                    GleanCrash.mozCrashReason.setIfNonNull(extras["MozCrashReason"])
                    GleanCrash.profilerChildShutdownPhase.setIfNonNull(extras["ProfilerChildShutdownPhase"])
                    GleanCrash.quotaManagerShutdownTimeout.setQuotaManagerShutdownTimeoutIfNonNull(
                        extras["QuotaManagerShutdownTimeout"],
                    )
                    GleanCrash.shutdownProgress.setIfNonNull(extras["ShutdownProgress"])
                    GleanCrash.stackTraces.setStackTracesIfNonNull(extras["StackTraces"])
                    // Overrides the original `startup` parameter to `Ping` when present
                    GleanCrash.startup.setIfNonNull(extras["StartupCrash"])

                    GleanEnvironment.headlessMode.setIfNonNull(extras["HeadlessMode"])

                    GleanMemory.availableCommit.setIfNonNull(extras["AvailablePageFile"])
                    GleanMemory.availablePhysical.setIfNonNull(extras["AvailablePhysicalMemory"])
                    GleanMemory.availableSwap.setIfNonNull(extras["AvailableSwapMemory"])
                    GleanMemory.availableVirtual.setIfNonNull(extras["AvailableVirtualMemory"])
                    GleanMemory.lowPhysical.setIfNonNull(extras["LowPhysicalMemoryEvents"])
                    GleanMemory.oomAllocationSize.setIfNonNull(extras["OOMAllocationSize"])
                    GleanMemory.purgeablePhysical.setIfNonNull(extras["PurgeablePhysicalMemory"])
                    GleanMemory.systemUsePercentage.setIfNonNull(extras["SystemMemoryUsePercentage"])
                    GleanMemory.texture.setIfNonNull(extras["TextureUsage"])
                    GleanMemory.totalPageFile.setIfNonNull(extras["TotalPageFile"])
                    GleanMemory.totalPhysical.setIfNonNull(extras["TotalPhysicalMemory"])
                    GleanMemory.totalVirtual.setIfNonNull(extras["TotalVirtualMemory"])
                }

                private fun StringMetricType.setIfNonNull(element: JsonElement?) {
                    element?.jsonPrimitive?.content?.let(::set)
                }

                private fun BooleanMetricType.setIfNonNull(element: JsonElement?) {
                    element?.jsonPrimitive?.content?.let { this.set(it == "1") }
                }

                private fun QuantityMetricType.setIfNonNull(element: JsonElement?) {
                    element?.jsonPrimitive?.long?.let(::set)
                }

                private fun ObjectMetricType<AsyncShutdownTimeoutObject>.setAsyncShutdownTimeoutIfNonNull(
                    element: JsonElement?,
                ) {
                    element?.jsonPrimitive?.content?.let { content ->
                        val a =
                            Json.decodeFromString<JsonObject>(content).mapValues { (key, value) ->
                                // The conditions object is sent as a serialized string.
                                if (key == "conditions") JsonPrimitive(value.toString()) else value
                            }
                        set(Json.decodeFromJsonElement(JsonObject(a)))
                    }
                }

                private fun ObjectMetricType<QuotaManagerShutdownTimeoutObject>.setQuotaManagerShutdownTimeoutIfNonNull(
                    element: JsonElement?,
                ) {
                    element?.let {
                        // The Glean metric is an array of the lines.
                        set(
                            Json.decodeFromJsonElement(
                                JsonArray(
                                    it.jsonPrimitive.content.split('\n')
                                        .map { s -> JsonPrimitive(s) },
                                ),
                            ),
                        )
                    }
                }

                private fun crashInfoEntries(crashInfo: JsonObject): List<Pair<String, JsonElement>> =
                    crashInfo.mapNotNull { (k, v) ->
                        when (k) {
                            "type" -> "crashType"
                            "address" -> "crashAddress"
                            "crashing_thread" -> "crashThread"
                            else -> null
                        }?.let { it to v }
                    }

                private fun modulesEntries(modules: JsonArray): List<Pair<String, JsonElement>> =
                    listOf(
                        "modules" to modules.map {
                            it.jsonObject.mapNotNull { (key, value) ->
                                when (key) {
                                    "base_addr" -> "baseAddress"
                                    "end_addr" -> "endAddress"
                                    "code_id" -> "codeId"
                                    "debug_file" -> "debugFile"
                                    "debug_id" -> "debugId"
                                    "filename", "version" -> key
                                    else -> null
                                }?.let { it to value }
                            }.toMap()
                                .let(::JsonObject)
                        }.let(::JsonArray),
                    )

                private fun threadsEntries(threads: JsonArray): List<Pair<String, JsonElement>> =
                    listOf(
                        "threads" to threads.map {
                            it.jsonObject.mapNotNull { (key, value) ->
                                when (key) {
                                    "frames" -> "frames" to value.jsonArray.map { frame ->
                                        frame.jsonObject.mapNotNull { (k, v) ->
                                            when (k) {
                                                "module_index" -> "moduleIndex"
                                                "ip", "trust" -> k
                                                else -> null
                                            }?.let { it to v }
                                        }.toMap().let(::JsonObject)
                                    }.let(::JsonArray)

                                    else -> null
                                }
                            }.toMap()
                                .let(::JsonObject)
                        }.let(::JsonArray),
                    )

                private fun ObjectMetricType<StackTracesObject>.setStackTracesIfNonNull(
                    element: JsonElement?,
                ) {
                    // No matter what happens, if an exception occurs we need to catch it to prevent
                    // it from stopping the ping from being sent. Stack traces are optional data, so
                    // we'd rather send a ping without it than not send the ping at all.
                    @Suppress("TooGenericExceptionCaught")
                    try {
                        element?.jsonObject?.let { obj ->
                            // The Glean metric has a slightly different layout. We
                            // explicitly set/filter to the values we support, in
                            // case there are new values in the object.
                            val m = obj.mapNotNull { (key, value) ->
                                when (key) {
                                    "status" -> if (value.jsonPrimitive.content != "OK") {
                                        listOf("error" to value)
                                    } else {
                                        null
                                    }

                                    "crash_info" -> (value as? JsonObject)?.let(::crashInfoEntries)
                                    "modules" -> (value as? JsonArray)?.let(::modulesEntries)
                                    "threads" -> (value as? JsonArray)?.let(::threadsEntries)
                                    "main_module" -> listOf("mainModule" to value)
                                    else -> null
                                }
                            }
                                .flatten()
                                .toMap()
                            set(Json.decodeFromJsonElement(JsonObject(m)))
                        }
                    } catch (e: Exception) {
                        logger.error("failed to populate stackTraces field: bad JSON input", e)
                    }
                }
            }

            @Serializable
            @SerialName("java_exception")
            data class JavaException(
                val throwableJson: JsonElement,
                val breadcrumbs: List<Breadcrumb>? = null,
            ) : PingCause() {
                override fun setMetrics() {
                    GleanCrash.cause.set("java_exception")
                    GleanCrash.javaException.set(
                        Json.decodeFromJsonElement<GleanCrash.JavaExceptionObject>(throwableJson),
                    )
                }
            }
        }

        @Serializable
        @SerialName("ping")
        data class Ping(
            val uptimeNanos: Long,
            val processType: String,
            val timeMillis: Long,
            val reason: Pings.crashReasonCodes,
            val cause: PingCause,
            val breadcrumbs: List<Breadcrumb> = listOf(),
            val startup: Boolean = false,
        ) : GleanCrashAction() {
            override fun submit() {
                GleanEnvironment.uptime.setRawNanos(uptimeNanos)
                GleanCrash.processType.set(processType)
                GleanCrash.time.set(Date(timeMillis))
                GleanCrash.startup.set(startup)
                cause.setMetrics()

                if (breadcrumbs.isNotEmpty()) {
                    GleanCrash.breadcrumbs.set(
                        Json.decodeFromJsonElement<GleanCrash.BreadcrumbsObject>(
                            JsonArray(
                                breadcrumbs.map { breadcrumb ->
                                    JsonObject(
                                        mapOf(
                                            "timestamp" to JsonPrimitive(breadcrumb.timestamp),
                                            "category" to JsonPrimitive(breadcrumb.category),
                                            "type" to JsonPrimitive(breadcrumb.type),
                                            "level" to JsonPrimitive(breadcrumb.level),
                                            "message" to JsonPrimitive(breadcrumb.message),
                                            "data" to JsonArray(
                                                breadcrumb.data.map {
                                                    JsonObject(
                                                        mapOf(
                                                            "key" to JsonPrimitive(it.key),
                                                            "value" to JsonPrimitive(it.value),
                                                        ),
                                                    )
                                                },
                                            ),
                                        ),
                                    )
                                },
                            ),
                        ),
                    )
                }

                Pings.crash.submit(reason)
            }
        }
    }

    private val creationTime = SystemClock.elapsedRealtimeNanos()

    init {
        run {
            // We only want to record things on the main process because that is the only one in which
            // Glean is properly initialized.  Checking to see if we are on the main process here will
            // prevent the situation that arises because the integrating app's Application will be
            // re-created when prompting to report the crash, and Glean is not initialized there since
            // it's not technically the main process.
            if (!context.isMainProcess()) {
                logger.info("GleanCrashReporterService initialized off of main process")
                return@run
            }

            if (!checkFileConditions()) {
                // checkFileConditions() internally logs error conditions
                return@run
            }

            // Parse the persisted crashes
            parseCrashFile()

            // Clear persisted counts by deleting the file
            file.delete()
        }
    }

    /**
     * Calculates the application uptime based on the creation time of this class (assuming it is
     * created in the application's `OnCreate`).
     */
    private fun uptime() = SystemClock.elapsedRealtimeNanos() - creationTime

    /**
     * Checks the file conditions to ensure it can be opened and read.
     *
     * @return True if the file exists and is able to be read, otherwise false
     */
    @VisibleForTesting(otherwise = VisibleForTesting.PRIVATE)
    internal fun checkFileConditions(): Boolean {
        return if (!file.exists()) {
            // This is just an info line, as most of the time we hope there is no file which means
            // there were no crashes
            logger.info("No crashes to record, or file not found.")
            false
        } else if (!file.canRead()) {
            logger.error("Cannot read file")
            false
        } else if (!file.isFile) {
            logger.error("Expected file, but found directory")
            false
        } else {
            true
        }
    }

    /**
     * Parses the crashes collected in the persisted crash file. The format of this file is simple,
     * a stream of serialized JSON GleanCrashAction objects.
     *
     * Example:
     *
     * <--Beginning of file-->
     * {"type":"count","label":"uncaught_exception"}\n
     * {"type":"count","label":"uncaught_exception"}\n
     * {"type":"count","label":"main_process_native_code_crash"}\n
     * {"type":"ping","uptimeNanos":2000000,"processType":"main","timeMillis":42000000000,
     *  "startup":false}\n
     * <--End of file-->
     *
     * It is unlikely that there will be more than one crash in a file, but not impossible.  This
     * could happen, for instance, if the application crashed again before the file could be
     * processed.
     */
    @Suppress("ComplexMethod")
    @VisibleForTesting(otherwise = VisibleForTesting.PRIVATE)
    internal fun parseCrashFile() {
        try {
            @OptIn(ExperimentalSerializationApi::class)
            val actionSequence = Json.decodeToSequence<GleanCrashAction>(
                file.inputStream(),
                DecodeSequenceMode.WHITESPACE_SEPARATED,
            )
            for (action in actionSequence) {
                action.submit()
            }
        } catch (e: IOException) {
            logger.error("Error reading crash file", e)
            return
        } catch (e: SerializationException) {
            logger.error("Error deserializing crash file", e)
            return
        }
    }

    /**
     * This function handles the actual recording of the crash to the persisted crash file. We are
     * only guaranteed runtime for the lifetime of the [CrashReporterService.report] function,
     * anything that we do in this function **MUST** be synchronous and blocking.  We cannot spawn
     * work to background processes or threads here if we want to guarantee that the work is
     * completed. Also, since the [CrashReporterService.report] functions are called synchronously,
     * and from lib-crash's own process, it is unlikely that this would be called from more than one
     * place at the same time.
     *
     * @param action Pass in the crash action to record.
     */
    @VisibleForTesting(otherwise = VisibleForTesting.PRIVATE)
    internal fun recordCrashAction(action: GleanCrashAction) {
        // Persist the crash in a file so that it can be recorded on the next application start. We
        // cannot directly record to Glean here because CrashHandler process is not the same process
        // as Glean is initialized in.
        // Create the file if it doesn't exist
        if (!file.exists()) {
            try {
                file.createNewFile()
            } catch (e: IOException) {
                logger.error("Failed to create crash file", e)
            }
        }

        // Add a line representing the crash that was received
        if (file.canWrite()) {
            try {
                @OptIn(ExperimentalSerializationApi::class)
                Json.encodeToStream(action, FileOutputStream(file, true))
                file.appendText("\n")
            } catch (e: IOException) {
                logger.error("Failed to write to crash file", e)
            }
        }
    }

    override fun record(crash: Crash.UncaughtExceptionCrash) {
        recordCrashAction(GleanCrashAction.Count(UNCAUGHT_EXCEPTION_KEY))
        recordCrashAction(
            GleanCrashAction.Ping(
                uptimeNanos = uptime(),
                processType = "main",
                timeMillis = crash.timestamp,
                reason = Pings.crashReasonCodes.crash,
                cause = GleanCrashAction.PingCause.JavaException(crash.throwable.toJson()),
                breadcrumbs = crash.breadcrumbs.map { it.toBreadcrumb() },
            ),
        )
    }

    private fun getExtrasJson(path: String): JsonObject? {
        val extrasFile = File(path)
        if (extrasFile.exists()) {
            try {
                @OptIn(ExperimentalSerializationApi::class)
                return Json.decodeFromStream<JsonObject>(extrasFile.inputStream())
            } catch (e: IOException) {
                logger.error("Error reading crash extra file", e)
            } catch (e: SerializationException) {
                logger.error("Error deserializing crash extra file", e)
            }
        } else {
            logger.warn("Crash extra file missing: $path")
        }
        return null
    }

    @Throws()
    private fun calculateMinidumpHash(path: String): String? {
        val minidumpFile = File(path)
        if (!minidumpFile.exists()) return null
        try {
            val digest = MessageDigest.getInstance("SHA-256")
            val input = minidumpFile.inputStream()

            val buffer = ByteArray(MINIDUMP_READ_BUFFER_SIZE)
            var n = 0
            while (n != -1) {
                digest.update(buffer, 0, n)
                n = input.read(buffer)
            }

            val hexString = StringBuilder()
            for (b in digest.digest()) {
                hexString.append("%02x".format(b))
            }
            return hexString.toString()
        } catch (e: IOException) {
            logger.error("Failed to generate minidump hash", e)
            return null
        }
    }

    private fun Throwable.toJson(): JsonElement {
        val throwables = generateSequence(this) { it.cause }
            .mapIndexed { index, throwable ->
                JsonObject(
                    mapOf(
                        "typeName" to throwable.javaClass.kotlin.qualifiedName.let(::JsonPrimitive),
                        "message" to throwable.message.let(::JsonPrimitive),
                        "stack" to if (index != 0) { JsonNull } else {
                            JsonArray(
                                throwable.stackTrace.map { element ->
                                    mapOf(
                                        "file" to element.fileName.let(::JsonPrimitive),
                                        "line" to element.lineNumber.takeIf { it >= 0 }.let(::JsonPrimitive),
                                        "className" to element.className.let(::JsonPrimitive),
                                        "methodName" to element.methodName.let(::JsonPrimitive),
                                        "isNative" to JsonPrimitive(element.isNativeMethod),
                                    ).filterValues { it !is JsonNull }.let(::JsonObject)
                                },
                            )
                        },
                    ).filterValues { it !is JsonNull },
                )
            }
            .toList()

        return JsonObject(
            mapOf(
                "throwables" to JsonArray(throwables),
            ),
        )
    }

    override fun record(crash: Crash.NativeCodeCrash) {
        when (crash.processType) {
            Crash.NativeCodeCrash.PROCESS_TYPE_MAIN ->
                recordCrashAction(GleanCrashAction.Count(MAIN_PROCESS_NATIVE_CODE_CRASH_KEY))
            Crash.NativeCodeCrash.PROCESS_TYPE_FOREGROUND_CHILD ->
                recordCrashAction(
                    GleanCrashAction.Count(
                        FOREGROUND_CHILD_PROCESS_NATIVE_CODE_CRASH_KEY,
                    ),
                )
            Crash.NativeCodeCrash.PROCESS_TYPE_BACKGROUND_CHILD ->
                recordCrashAction(
                    GleanCrashAction.Count(
                        BACKGROUND_CHILD_PROCESS_NATIVE_CODE_CRASH_KEY,
                    ),
                )
        }

        // The `processType` property on a crash is a bit confusing because it does not map to the actual process types
        // (like main, content, gpu, etc.). This property indicates what UI we should show to users given that "main"
        // crashes essentially kill the app, "foreground child" crashes are likely tab crashes, and "background child"
        // crashes are occurring in other processes (like GPU and extensions) for which users shouldn't notice anything
        // (because there shouldn't be any noticeable impact in the app and the processes will be recreated
        // automatically).
        val processType = when (crash.processType) {
            Crash.NativeCodeCrash.PROCESS_TYPE_MAIN -> "main"

            Crash.NativeCodeCrash.PROCESS_TYPE_BACKGROUND_CHILD -> {
                when (crash.remoteType) {
                    // The extensions process is a content process as per:
                    // https://firefox-source-docs.mozilla.org/dom/ipc/process_model.html#webextensions
                    "extension" -> "content"

                    else -> "utility"
                }
            }

            Crash.NativeCodeCrash.PROCESS_TYPE_FOREGROUND_CHILD -> "content"

            else -> "main"
        }

        if (crash.minidumpPath != null && crash.extrasPath != null) {
            MinidumpAnalyzer.load()?.run(crash.minidumpPath, crash.extrasPath, false)
        }

        val extrasJson = crash.extrasPath?.let { getExtrasJson(it) }

        val minidumpHash = crash.minidumpPath?.let { calculateMinidumpHash(it) }

        recordCrashAction(
            GleanCrashAction.Ping(
                uptimeNanos = uptime(),
                processType = processType,
                timeMillis = crash.timestamp,
                reason = Pings.crashReasonCodes.crash,
                cause = GleanCrashAction.PingCause.OsFault(
                    remoteType = crash.remoteType,
                    extras = extrasJson,
                    minidumpHash = minidumpHash,
                ),
                breadcrumbs = crash.breadcrumbs.map { it.toBreadcrumb() },
            ),
        )
    }

    override fun record(throwable: Throwable) {
        recordCrashAction(GleanCrashAction.Count(CAUGHT_EXCEPTION_KEY))
    }
}
