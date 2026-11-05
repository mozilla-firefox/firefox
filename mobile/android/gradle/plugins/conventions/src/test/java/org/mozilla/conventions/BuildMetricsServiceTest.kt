/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.conventions

import groovy.json.JsonSlurper
import org.gradle.tooling.events.task.*
import org.junit.jupiter.api.Assertions.*
import org.junit.jupiter.api.Test
import org.junit.jupiter.api.io.TempDir
import org.mockito.Mockito.*
import java.io.File
import java.lang.management.ManagementFactory
import java.util.UUID

class BuildMetricsServiceTest {
    @TempDir
    lateinit var tempDir: File

    @Test
    fun `service records different task statuses correctly`() {
        val suffix = "task-status"
        val service = createService(tempDir, suffix)

        service.onFinish(createTaskEvent(":app:compileJava", upToDate = true))
        service.onFinish(createTaskEvent(":lib:test", fromCache = true))
        service.onFinish(createTaskEvent(":app:build"))
        service.onFinish(createTaskEvent(":app:failedTask", failed = true))
        service.onFinish(createTaskEvent(":app:skippedTask", skipped = true))

        service.close()

        val metricsFile = File(tempDir, "build-metrics-$suffix.json")
        assertTrue(metricsFile.exists())

        val metrics = JsonSlurper().parseText(metricsFile.readText()) as Map<*, *>
        val tasks = metrics["tasks"] as List<*>
        assertEquals(5, tasks.size)

        val taskMap = tasks.associate { task ->
            val t = task as Map<*, *>
            t["path"] to t["status"]
        }

        assertEquals("UP-TO-DATE", taskMap[":app:compileJava"])
        assertEquals("FROM-CACHE", taskMap[":lib:test"])
        assertEquals("EXECUTED", taskMap[":app:build"])
        assertEquals("FAILED", taskMap[":app:failedTask"])
        assertEquals("SKIPPED", taskMap[":app:skippedTask"])
    }

    @Test
    fun `service creates metrics file with correct structure`() {
        val suffix = "structure"
        val service = createService(tempDir, suffix)

        service.onFinish(createTaskEvent(":task1"))
        service.onFinish(createTaskEvent(":task2", upToDate = true))

        service.close()

        val metricsFile = File(tempDir, "build-metrics-$suffix.json")
        val metrics = JsonSlurper().parseText(metricsFile.readText()) as Map<*, *>

        assertNotNull(metrics["invocation"])
        assertNotNull(metrics["configPhase"])
        assertNotNull(metrics["executionPhase"])
        assertEquals(false, metrics["configurationCacheReused"])
        assertNotNull(metrics["tasks"])

        val tasks = metrics["tasks"] as List<*>
        assertEquals(2, tasks.size)
    }

    @Test
    fun `service uses custom file suffix`() {
        val suffix = "custom-suffix"
        val service = createService(tempDir, suffix)

        service.onFinish(createTaskEvent(":task1"))
        service.close()

        val metricsFile = File(tempDir, "build-metrics-$suffix.json")
        assertTrue(metricsFile.exists())
    }

    @Test
    fun `default file suffix is the invocation end timestamp`() {
        val service = createService(tempDir, fileSuffix = null)

        service.onFinish(createTaskEvent(":task1"))
        service.close()

        val files = tempDir.listFiles()!!.map { it.name }
        assertEquals(1, files.size)
        assertTrue(
            Regex("""build-metrics-\d{4}-\d{2}-\d{2}-\d{2}-\d{2}-\d{2}-\d{3}\.json""").matches(files[0]),
            "unexpected file name ${files[0]}",
        )
    }

    @Test
    fun `tasks appear in execution order`() {
        val suffix = "order"
        val service = createService(tempDir, suffix)

        service.onFinish(createTaskEvent(":second", startMs = 2000L, endMs = 2500L))
        service.onFinish(createTaskEvent(":first", startMs = 1000L, endMs = 3000L))
        service.onFinish(createTaskEvent(":third", startMs = 2600L, endMs = 2700L))

        service.close()

        val metricsFile = File(tempDir, "build-metrics-$suffix.json")
        val metrics = JsonSlurper().parseText(metricsFile.readText()) as Map<*, *>
        val tasks = metrics["tasks"] as List<*>

        assertEquals(":first", (tasks[0] as Map<*, *>)["path"])
        assertEquals(":second", (tasks[1] as Map<*, *>)["path"])
        assertEquals(":third", (tasks[2] as Map<*, *>)["path"])
    }

    @Test
    fun `task timing fields are present in JSON`() {
        val suffix = "timing"
        val service = createService(tempDir, suffix)

        service.onFinish(createTaskEvent(":testTask", startMs = 1000L, endMs = 2500L))
        service.close()

        val metricsFile = File(tempDir, "build-metrics-$suffix.json")
        val metrics = JsonSlurper().parseText(metricsFile.readText()) as Map<*, *>
        val task = (metrics["tasks"] as List<*>).single() as Map<*, *>

        assertNotNull(task["start"])
        assertNotNull(task["end"])
        assertEquals("1.500", task["duration"])
        assertEquals(1000L, longValue(task["startMs"]))
        assertEquals(2500L, longValue(task["endMs"]))
        assertEquals(1500L, longValue(task["durationMs"]))
    }

    @Test
    fun `phases are derived from the JVM start, the task graph readiness and the task timestamps`() {
        val suffix = "phases"
        val service = createService(tempDir, suffix, configStartMs = 1500L, configEndMs = 1800L)

        service.onFinish(createTaskEvent(":plugin:compileKotlin", startMs = 1600L, endMs = 1700L))
        service.onFinish(createTaskEvent(":late", startMs = 2500L, endMs = 4000L))
        service.onFinish(createTaskEvent(":early", startMs = 2000L, endMs = 3000L))

        service.close()

        val metricsFile = File(tempDir, "build-metrics-$suffix.json")
        val metrics = JsonSlurper().parseText(metricsFile.readText()) as Map<*, *>

        val invocation = metrics["invocation"] as Map<*, *>
        assertEquals(ManagementFactory.getRuntimeMXBean().startTime, longValue(invocation["startMs"]))
        assertEquals("jvmStart", invocation["startMeasuredFrom"])
        assertTrue(longValue(invocation["endMs"]) >= 4000L)

        val configPhase = metrics["configPhase"] as Map<*, *>
        assertEquals(1500L, longValue(configPhase["startMs"]))
        assertEquals(1800L, longValue(configPhase["endMs"]))
        assertEquals(300L, longValue(configPhase["durationMs"]))

        val executionPhase = metrics["executionPhase"] as Map<*, *>
        assertEquals(1800L, longValue(executionPhase["startMs"]))
        assertEquals(4000L, longValue(executionPhase["endMs"]))
        assertEquals(2200L, longValue(executionPhase["durationMs"]))
        assertEquals("2.200", executionPhase["duration"])
    }

    @Test
    fun `configuration without a ready task graph ends at the first task start`() {
        val suffix = "no-graph"
        val service = createService(tempDir, suffix, configStartMs = 1500L, configEndMs = null)

        service.onFinish(createTaskEvent(":task", startMs = 2000L, endMs = 3000L))
        service.close()

        val metricsFile = File(tempDir, "build-metrics-$suffix.json")
        val metrics = JsonSlurper().parseText(metricsFile.readText()) as Map<*, *>

        val configPhase = metrics["configPhase"] as Map<*, *>
        assertEquals(2000L, longValue(configPhase["endMs"]))
        assertEquals(1000L, longValue((metrics["executionPhase"] as Map<*, *>)["durationMs"]))
    }

    @Test
    fun `phases without tasks end at the invocation end`() {
        val suffix = "no-tasks"
        val service = createService(tempDir, suffix, configStartMs = 1500L, configEndMs = 1800L)

        service.close()

        val metricsFile = File(tempDir, "build-metrics-$suffix.json")
        val metrics = JsonSlurper().parseText(metricsFile.readText()) as Map<*, *>

        val invocation = metrics["invocation"] as Map<*, *>
        val configPhase = metrics["configPhase"] as Map<*, *>
        val executionPhase = metrics["executionPhase"] as Map<*, *>

        assertEquals(1800L, longValue(configPhase["endMs"]))
        assertEquals(1800L, longValue(executionPhase["startMs"]))
        assertEquals(longValue(invocation["endMs"]), longValue(executionPhase["endMs"]))
        assertTrue((metrics["tasks"] as List<*>).isEmpty())
    }

    @Test
    fun `reused configuration reports no configuration phase`() {
        val suffix = "reused"
        val service = createService(tempDir, suffix, configured = false)

        service.onFinish(createTaskEvent(":task", startMs = 2000L, endMs = 3000L))
        service.close()

        val metricsFile = File(tempDir, "build-metrics-$suffix.json")
        val metrics = JsonSlurper().parseText(metricsFile.readText()) as Map<*, *>

        assertEquals(true, metrics["configurationCacheReused"])
        assertNotNull(metrics["invocation"])
        assertFalse(metrics.containsKey("configPhase"))
        assertEquals(1000L, longValue((metrics["executionPhase"] as Map<*, *>)["durationMs"]))
        assertEquals(1, (metrics["tasks"] as List<*>).size)
    }

    @Test
    fun `consuming a build leaves other recorded builds alone`() {
        val other = UUID.randomUUID().toString()
        ConfiguredBuilds.recordConfigStart(other)

        val service = createService(tempDir, "isolation")
        service.close()

        assertNotNull(ConfiguredBuilds.consume(other))
        assertNull(ConfiguredBuilds.consume(other))
    }

    @Test
    fun `service handles different task path formats`() {
        val suffix = "paths"
        val service = createService(tempDir, suffix)

        service.onFinish(createTaskEvent(":rootTask"))
        service.onFinish(createTaskEvent(":project:subTask"))
        service.onFinish(createTaskEvent(":deep:nested:project:deepTask"))

        service.close()

        val metricsFile = File(tempDir, "build-metrics-$suffix.json")
        val metrics = JsonSlurper().parseText(metricsFile.readText()) as Map<*, *>
        val tasks = metrics["tasks"] as List<*>

        val taskPaths = tasks.map { (it as Map<*, *>)["path"] }
        assertTrue(taskPaths.contains(":rootTask"))
        assertTrue(taskPaths.contains(":project:subTask"))
        assertTrue(taskPaths.contains(":deep:nested:project:deepTask"))
    }

    @Test
    fun `service creates metrics directory if it does not exist`() {
        val suffix = "newdir"
        val newTempDir = File(tempDir, "nonexistent")
        val service = createService(newTempDir, suffix)

        service.onFinish(createTaskEvent(":task"))
        service.close()

        assertTrue(newTempDir.exists())
        assertTrue(newTempDir.isDirectory)

        val metricsFile = File(newTempDir, "build-metrics-$suffix.json")
        assertTrue(metricsFile.exists())
    }

    private fun longValue(value: Any?): Long = (value as Number).toLong()

    private fun <T : Any> mockProperty(value: T?): org.gradle.api.provider.Property<T> {
        val property = mock<org.gradle.api.provider.Property<T>>()
        `when`(property.orNull).thenReturn(value)
        if (value != null) {
            `when`(property.get()).thenReturn(value)
        }
        return property
    }

    private fun createService(
        outputDir: File,
        fileSuffix: String?,
        configStartMs: Long = 800L,
        configEndMs: Long? = 900L,
        configured: Boolean = true,
    ): BuildMetricsService {
        val buildId = UUID.randomUUID().toString()
        val parameters = object : BuildMetricsServiceParameters {
            override val outputDir = mockProperty(outputDir.absolutePath)
            override val fileSuffix = mockProperty(fileSuffix)
            override val buildId = mockProperty(buildId)
            override val configStartMs = mockProperty(configStartMs)
        }
        if (configured) {
            ConfiguredBuilds.recordConfigStart(buildId)
            configEndMs?.let { ConfiguredBuilds.recordConfigEnd(buildId, it) }
        }
        return TestBuildMetricsService(parameters)
    }

    private fun createTaskEvent(
        taskPath: String,
        upToDate: Boolean = false,
        fromCache: Boolean = false,
        failed: Boolean = false,
        skipped: Boolean = false,
        startMs: Long = 1000L,
        endMs: Long = 2000L,
    ): TaskFinishEvent {
        val result = if (failed) mockFailedResult(startMs, endMs) else if (skipped) mockSkippedResult(startMs, endMs) else mockSuccessResult(upToDate, fromCache, startMs, endMs)
        val descriptor = mock(TaskOperationDescriptor::class.java).apply { `when`(getTaskPath()).thenReturn(taskPath) }
        return mock(TaskFinishEvent::class.java).apply { `when`(getDescriptor()).thenReturn(descriptor); `when`(getResult()).thenReturn(result) }
    }

    private fun mockSuccessResult(upToDate: Boolean, fromCache: Boolean, startMs: Long, endMs: Long) = mock(TaskSuccessResult::class.java).apply {
        `when`(getStartTime()).thenReturn(startMs); `when`(getEndTime()).thenReturn(endMs)
        `when`(isUpToDate()).thenReturn(upToDate); `when`(isFromCache()).thenReturn(fromCache)
    }

    private fun mockFailedResult(startMs: Long, endMs: Long) = mock(TaskFailureResult::class.java).apply {
        `when`(getStartTime()).thenReturn(startMs); `when`(getEndTime()).thenReturn(endMs)
    }

    private fun mockSkippedResult(startMs: Long, endMs: Long) = mock(TaskSkippedResult::class.java).apply {
        `when`(getStartTime()).thenReturn(startMs); `when`(getEndTime()).thenReturn(endMs)
    }

    class TestBuildMetricsService(private val testParameters: BuildMetricsServiceParameters) : BuildMetricsService(testParameters) {
        override fun getParameters() = testParameters
    }
}
