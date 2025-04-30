/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.lib.crash.service

import android.content.Context
import androidx.test.core.app.ApplicationProvider
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.work.testing.WorkManagerTestInitHelper
import kotlinx.serialization.json.Json
import kotlinx.serialization.json.JsonArray
import kotlinx.serialization.json.JsonObject
import kotlinx.serialization.json.JsonPrimitive
import kotlinx.serialization.json.jsonArray
import kotlinx.serialization.json.jsonObject
import mozilla.components.concept.base.crash.Breadcrumb
import mozilla.components.lib.crash.Crash
import mozilla.components.lib.crash.GleanMetrics.CrashMetrics
import mozilla.components.support.test.whenever
import mozilla.telemetry.glean.Glean
import mozilla.telemetry.glean.config.Configuration
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder
import org.junit.runner.RunWith
import org.mockito.Mockito.spy
import java.io.File
import java.util.Calendar
import java.util.Date
import java.util.GregorianCalendar
import mozilla.components.lib.crash.GleanMetrics.Crash as GleanCrash
import mozilla.components.lib.crash.GleanMetrics.Environment as GleanEnvironment
import mozilla.components.lib.crash.GleanMetrics.Memory as GleanMemory
import mozilla.components.lib.crash.GleanMetrics.Pings as GleanPings

@RunWith(AndroidJUnit4::class)
class GleanCrashReporterServiceTest {
    private val context: Context
        get() = ApplicationProvider.getApplicationContext()

    @Before
    fun setUp() {
        // We're using the WorkManager in a bunch of places, and Glean will crash
        // in tests without this line. Let's simply put it here.
        WorkManagerTestInitHelper.initializeTestWorkManager(context)
        Glean.registerPings(GleanPings)
        Glean.resetGlean(
            context = context,
            config = Configuration(),
            clearStores = true,
        )
    }

    @After
    fun tearDown() {
        // This closes the database to help prevent leaking it during tests.
        // See Bug1719905 for more info.
        WorkManagerTestInitHelper.closeWorkDatabase()
    }

    @get:Rule
    val tempFolder = TemporaryFolder()

    private fun crashCountJson(key: String): String = """{"type":"count","label":"$key"}"""

    private fun crashPingJson(uptime: Long, type: String, time: Long): String =
        """{"type":"ping","uptimeNanos":$uptime,"processType":"$type","timeMillis":$time,"reason":"crash","cause":{"type":"os_fault","remoteType":null,"extras":null,"minidumpHash":null}}"""

    private fun crashPingJsonWithRemoteType(
        uptime: Long,
        type: String,
        time: Long,
        remoteType: String,
    ): String =
        """{"type":"ping","uptimeNanos":$uptime,"processType":"$type","timeMillis":$time,"reason":"crash","cause":{"type":"os_fault","remoteType":"$remoteType","extras":null,"minidumpHash":null}}"""

    private fun exceptionPingJson(uptime: Long, time: Long): Regex =
        Regex("""\{"type":"ping","uptimeNanos":$uptime,"processType":"main","timeMillis":$time,"reason":"crash","cause":\{"type":"java_exception",.*}}""")

    @Test
    fun `GleanCrashReporterService records all crash types`() {
        val crashTypes = hashMapOf(
            GleanCrashReporterService.MAIN_PROCESS_NATIVE_CODE_CRASH_KEY to Crash.NativeCodeCrash(
                0,
                "",
                "",
                Crash.NativeCodeCrash.PROCESS_TYPE_MAIN,
                breadcrumbs = arrayListOf(),
                remoteType = null,
            ),
            GleanCrashReporterService.FOREGROUND_CHILD_PROCESS_NATIVE_CODE_CRASH_KEY to Crash.NativeCodeCrash(
                0,
                "",
                "",
                Crash.NativeCodeCrash.PROCESS_TYPE_FOREGROUND_CHILD,
                breadcrumbs = arrayListOf(),
                remoteType = "web",
            ),
            GleanCrashReporterService.BACKGROUND_CHILD_PROCESS_NATIVE_CODE_CRASH_KEY to Crash.NativeCodeCrash(
                0,
                "",
                "",
                Crash.NativeCodeCrash.PROCESS_TYPE_BACKGROUND_CHILD,
                breadcrumbs = arrayListOf(),
                remoteType = null,
            ),
            GleanCrashReporterService.UNCAUGHT_EXCEPTION_KEY to Crash.UncaughtExceptionCrash(
                0,
                RuntimeException("Test"),
                arrayListOf(),
            ),
            GleanCrashReporterService.CAUGHT_EXCEPTION_KEY to RuntimeException("Test"),
        )

        for ((type, crash) in crashTypes) {
            // Because of how Glean is implemented, it can potentially persist information between
            // tests or even between test classes, so we compensate by capturing the initial value
            // to compare to.
            val initialValue = try {
                CrashMetrics.crashCount[type].testGetValue()!!
            } catch (e: NullPointerException) {
                0
            }

            run {
                val service = spy(GleanCrashReporterService(context))

                assertFalse("No previous persisted crashes must exist", service.file.exists())

                when (crash) {
                    is Crash.NativeCodeCrash -> service.record(crash)
                    is Crash.UncaughtExceptionCrash -> service.record(crash)
                    is Throwable -> service.record(crash)
                }

                assertTrue("Persistence file must exist", service.file.exists())
                val lines = service.file.readLines()
                assertEquals(
                    "Must be $type",
                    crashCountJson(type),
                    lines.first(),
                )
            }

            // Initialize a fresh GleanCrashReporterService and ensure metrics are recorded in Glean
            run {
                GleanCrashReporterService(context)

                assertEquals(
                    "Glean must record correct value",
                    1,
                    CrashMetrics.crashCount[type].testGetValue()!! - initialValue,
                )
            }
        }
    }

    @Test
    fun `GleanCrashReporterService correctly handles multiple crashes in a single file`() {
        val initialExceptionValue = try {
            CrashMetrics.crashCount[GleanCrashReporterService.UNCAUGHT_EXCEPTION_KEY].testGetValue()!!
        } catch (e: NullPointerException) {
            0
        }
        val initialMainProcessNativeCrashValue = try {
            CrashMetrics.crashCount[GleanCrashReporterService.MAIN_PROCESS_NATIVE_CODE_CRASH_KEY].testGetValue()!!
        } catch (e: NullPointerException) {
            0
        }

        val initialForegroundChildProcessNativeCrashValue = try {
            CrashMetrics.crashCount[GleanCrashReporterService.FOREGROUND_CHILD_PROCESS_NATIVE_CODE_CRASH_KEY].testGetValue()!!
        } catch (e: NullPointerException) {
            0
        }

        val initialBackgroundChildProcessNativeCrashValue = try {
            CrashMetrics.crashCount[GleanCrashReporterService.BACKGROUND_CHILD_PROCESS_NATIVE_CODE_CRASH_KEY].testGetValue()!!
        } catch (e: NullPointerException) {
            0
        }

        run {
            val service = spy(GleanCrashReporterService(context))

            assertFalse("No previous persisted crashes must exist", service.file.exists())

            val uncaughtExceptionCrash =
                Crash.UncaughtExceptionCrash(0, RuntimeException("Test"), arrayListOf())
            val mainProcessNativeCodeCrash = Crash.NativeCodeCrash(
                0,
                "",
                "",
                Crash.NativeCodeCrash.PROCESS_TYPE_MAIN,
                breadcrumbs = arrayListOf(),
                remoteType = null,
            )
            val foregroundChildProcessNativeCodeCrash = Crash.NativeCodeCrash(
                0,
                "",
                "",
                Crash.NativeCodeCrash.PROCESS_TYPE_FOREGROUND_CHILD,
                breadcrumbs = arrayListOf(),
                remoteType = "web",
            )
            val backgroundChildProcessNativeCodeCrash = Crash.NativeCodeCrash(
                0,
                "",
                "",
                Crash.NativeCodeCrash.PROCESS_TYPE_BACKGROUND_CHILD,
                breadcrumbs = arrayListOf(),
                remoteType = null,
            )
            val extensionProcessNativeCodeCrash = Crash.NativeCodeCrash(
                0,
                "",
                "",
                Crash.NativeCodeCrash.PROCESS_TYPE_BACKGROUND_CHILD,
                breadcrumbs = arrayListOf(),
                remoteType = "extension",
            )

            // Record some crashes
            service.record(uncaughtExceptionCrash)
            service.record(mainProcessNativeCodeCrash)
            service.record(uncaughtExceptionCrash)
            service.record(foregroundChildProcessNativeCodeCrash)
            service.record(backgroundChildProcessNativeCodeCrash)
            service.record(extensionProcessNativeCodeCrash)

            // Make sure the file exists
            assertTrue("Persistence file must exist", service.file.exists())

            // Get the file lines
            val lines = service.file.readLines().iterator()
            assertEquals(
                "element must be uncaught exception",
                crashCountJson(GleanCrashReporterService.UNCAUGHT_EXCEPTION_KEY),
                lines.next(),
            )
            assertTrue(
                "element must be uncaught exception ping",
                exceptionPingJson(0, 0) matches lines.next(),
            )
            assertEquals(
                "element must be main process native code crash",
                crashCountJson(GleanCrashReporterService.MAIN_PROCESS_NATIVE_CODE_CRASH_KEY),
                lines.next(),
            )
            assertEquals(
                "element must be main process crash ping",
                crashPingJson(0, "main", 0),
                lines.next(),
            )
            assertEquals(
                "element must be uncaught exception",
                crashCountJson(GleanCrashReporterService.UNCAUGHT_EXCEPTION_KEY),
                lines.next(), // skip crash ping line in this test
            )
            assertTrue(
                "element must be uncaught exception ping",
                exceptionPingJson(0, 0) matches lines.next(),
            )
            assertEquals(
                "element must be foreground child process native code crash",
                crashCountJson(GleanCrashReporterService.FOREGROUND_CHILD_PROCESS_NATIVE_CODE_CRASH_KEY),
                lines.next(),
            )
            assertEquals(
                "element must be foreground process crash ping",
                crashPingJsonWithRemoteType(0, "content", 0, "web"),
                lines.next(),
            )
            assertEquals(
                "element must be background child process native code crash",
                crashCountJson(GleanCrashReporterService.BACKGROUND_CHILD_PROCESS_NATIVE_CODE_CRASH_KEY),
                lines.next(), // skip crash ping line
            )
            assertEquals(
                "element must be background process crash ping",
                crashPingJson(0, "utility", 0),
                lines.next(),
            )
            assertEquals(
                "element must be background child process native code crash",
                crashCountJson(GleanCrashReporterService.BACKGROUND_CHILD_PROCESS_NATIVE_CODE_CRASH_KEY),
                lines.next(),
            )
            assertEquals(
                "element must be extensions process crash ping",
                crashPingJsonWithRemoteType(0, "content", 0, "extension"),
                lines.next(),
            )
            assertFalse(lines.hasNext())
        }

        // Initialize a fresh GleanCrashReporterService and ensure metrics are recorded in Glean
        run {
            GleanCrashReporterService(context)

            assertEquals(
                "Glean must record correct value",
                2,
                CrashMetrics.crashCount[GleanCrashReporterService.UNCAUGHT_EXCEPTION_KEY].testGetValue()!! - initialExceptionValue,
            )
            assertEquals(
                "Glean must record correct value",
                1,
                CrashMetrics.crashCount[GleanCrashReporterService.MAIN_PROCESS_NATIVE_CODE_CRASH_KEY].testGetValue()!! - initialMainProcessNativeCrashValue,
            )
            assertEquals(
                "Glean must record correct value",
                1,
                CrashMetrics.crashCount[GleanCrashReporterService.FOREGROUND_CHILD_PROCESS_NATIVE_CODE_CRASH_KEY].testGetValue()!! - initialForegroundChildProcessNativeCrashValue,
            )
            assertEquals(
                "Glean must record correct value",
                2,
                CrashMetrics.crashCount[GleanCrashReporterService.BACKGROUND_CHILD_PROCESS_NATIVE_CODE_CRASH_KEY].testGetValue()!! - initialBackgroundChildProcessNativeCrashValue,
            )
        }
    }

    @Test
    fun `GleanCrashReporterService does not crash if it can't write to it's file`() {
        val file =
            spy(File(context.applicationInfo.dataDir, GleanCrashReporterService.CRASH_FILE_NAME))
        whenever(file.canWrite()).thenReturn(false)
        val service = spy(GleanCrashReporterService(context, file))

        assertFalse("No previous persisted crashes must exist", service.file.exists())

        val crash = Crash.UncaughtExceptionCrash(0, RuntimeException("Test"), arrayListOf())
        service.record(crash)

        assertTrue("Persistence file must exist", service.file.exists())
        val lines = service.file.readLines()
        assertEquals("Must be empty due to mocked write error", 0, lines.count())
    }

    @Test
    fun `GleanCrashReporterService does not crash if the persistent file is corrupted`() {
        // Because of how Glean is implemented, it can potentially persist information between
        // tests or even between test classes, so we compensate by capturing the initial value
        // to compare to.
        val initialValue = try {
            CrashMetrics.crashCount[GleanCrashReporterService.UNCAUGHT_EXCEPTION_KEY].testGetValue()!!
        } catch (e: NullPointerException) {
            0
        }

        run {
            val service = spy(GleanCrashReporterService(context))

            assertFalse("No previous persisted crashes must exist", service.file.exists())

            val crash = Crash.UncaughtExceptionCrash(
                0,
                RuntimeException("Test"),
                arrayListOf(),
            )
            service.record(crash)

            assertTrue("Persistence file must exist", service.file.exists())

            // Add bad data
            service.file.appendText("bad data in here\n")

            val lines = service.file.readLines()
            assertEquals(
                "must be native code crash",
                "{\"type\":\"count\",\"label\":\"${GleanCrashReporterService.UNCAUGHT_EXCEPTION_KEY}\"}",
                lines.first(),
            )
            assertTrue(
                "must be uncaught exception ping",
                exceptionPingJson(0, 0) matches lines[1],
            )
            assertEquals("bad data in here", lines[2])
        }

        run {
            GleanCrashReporterService(context)

            assertEquals(
                "Glean must record correct value",
                1,
                CrashMetrics.crashCount[GleanCrashReporterService.UNCAUGHT_EXCEPTION_KEY].testGetValue()!! - initialValue,
            )
        }
    }

    @Test
    fun `GleanCrashReporterService sends crash pings`() {
        val service = spy(GleanCrashReporterService(context))

        val crash = Crash.NativeCodeCrash(
            12340000,
            "",
            "",
            Crash.NativeCodeCrash.PROCESS_TYPE_MAIN,
            breadcrumbs = arrayListOf(),
            remoteType = null,
        )

        service.record(crash)

        assertTrue("Persistence file must exist", service.file.exists())

        val lines = service.file.readLines()
        assertEquals(
            "First element must be main process native code crash",
            crashCountJson(GleanCrashReporterService.MAIN_PROCESS_NATIVE_CODE_CRASH_KEY),
            lines[0],
        )
        assertEquals(
            "Second element must be main process crash ping",
            crashPingJson(0, "main", 12340000),
            lines[1],
        )

        run {
            var pingReceived = false
            GleanPings.crash.testBeforeNextSubmit { _ ->
                val date = GregorianCalendar().apply {
                    time = Date(12340000)
                }
                date.set(Calendar.SECOND, 0)
                date.set(Calendar.MILLISECOND, 0)
                assertEquals(date.time, GleanCrash.time.testGetValue())
                assertEquals(0L, GleanEnvironment.uptime.testGetValue())
                assertEquals("main", GleanCrash.processType.testGetValue())
                assertEquals(false, GleanCrash.startup.testGetValue())
                assertEquals("os_fault", GleanCrash.cause.testGetValue())
                assertNull(GleanCrash.remoteType.testGetValue())
                pingReceived = true
            }

            GleanCrashReporterService(context)
            assertTrue("Expected ping to be sent", pingReceived)
        }
    }

    @Test
    fun `GleanCrashReporterService sends breadcrumbs`() {
        val service = spy(GleanCrashReporterService(context))

        val crash = Crash.NativeCodeCrash(
            12340000,
            null,
            null,
            Crash.NativeCodeCrash.PROCESS_TYPE_MAIN,
            breadcrumbs = arrayListOf(
                Breadcrumb(
                    message = "Breadcrumb-1",
                    category = "bread",
                    level = Breadcrumb.Level.WARNING,
                    type = Breadcrumb.Type.USER,
                    date = Date(12340000),
                    data = mapOf("foo" to "bar"),
                ),
            ),
            remoteType = null,
        )

        service.record(crash)

        assertTrue("Persistence file must exist", service.file.exists())

        val lines = service.file.readLines()
        assertEquals(
            "First element must be main process native code crash",
            crashCountJson(GleanCrashReporterService.MAIN_PROCESS_NATIVE_CODE_CRASH_KEY),
            lines[0],
        )
        assertEquals(
            "Second element must be main process crash ping with breadcrumbs",
            """{"type":"ping","uptimeNanos":0,"processType":"main","timeMillis":12340000,
                "reason":"crash","cause":{"type":"os_fault","remoteType":null,"extras":null,
                "minidumpHash":null},"breadcrumbs":[
                    {"timestamp":"1970-01-01T03:25:40","message":"Breadcrumb-1","category":"bread","level":"Warning","type":"User","data":{"foo":"bar"}}
                ]}""".replace(Regex("\\s"), ""),
            lines[1],
        )

        run {
            var pingReceived = false
            GleanPings.crash.testBeforeNextSubmit { _ ->
                val date = GregorianCalendar().apply {
                    time = Date(12340000)
                }
                date.set(Calendar.SECOND, 0)
                date.set(Calendar.MILLISECOND, 0)
                assertEquals(date.time, GleanCrash.time.testGetValue())
                assertEquals(0L, GleanEnvironment.uptime.testGetValue())
                assertEquals("main", GleanCrash.processType.testGetValue())
                assertEquals(false, GleanCrash.startup.testGetValue())
                assertEquals("os_fault", GleanCrash.cause.testGetValue())
                assertNull(GleanCrash.remoteType.testGetValue())
                assertEquals(
                    JsonArray(
                        listOf(
                            JsonObject(
                                mapOf(
                                    "timestamp" to JsonPrimitive("1970-01-01T03:25:40"),
                                    "message" to JsonPrimitive("Breadcrumb-1"),
                                    "category" to JsonPrimitive("bread"),
                                    "level" to JsonPrimitive("Warning"),
                                    "type" to JsonPrimitive("User"),
                                    "data" to JsonArray(
                                        listOf(
                                            JsonObject(
                                                mapOf(
                                                    "key" to JsonPrimitive("foo"),
                                                    "value" to JsonPrimitive("bar"),
                                                ),
                                            ),
                                        ),
                                    ),
                                ),
                            ),
                        ),
                    ),
                    GleanCrash.breadcrumbs.testGetValue(),
                )
                pingReceived = true
            }

            GleanCrashReporterService(context)
            assertTrue("Expected ping to be sent", pingReceived)
        }
    }

    @Test
    fun `GleanCrashReporterService reads extras`() {
        val service = spy(GleanCrashReporterService(context))
        val stackTracesAnnotation = """
        {
            "status": "OK",
            "crash_info": {
                "type": "main",
                "address": "0xf001ba11",
                "crashing_thread": 1
            },
            "main_module": 0,
            "modules": [
            {
                "base_addr": "0x00000000",
                "end_addr": "0x00004000",
                "code_id": "8675309",
                "debug_file": "",
                "debug_id": "18675309",
                "filename": "foo.exe",
                "version": "1.0.0"
            },
            {
                "base_addr": "0x00004000",
                "end_addr": "0x00008000",
                "code_id": "42",
                "debug_file": "foo.pdb",
                "debug_id": "43",
                "filename": "foo.dll",
                "version": "1.1.0"
            }
            ],
            "some_unused_key": 0,
            "threads": [
            {
                "frames": [
                { "module_index": 0, "ip": "0x10", "trust": "context" },
                { "module_index": 0, "ip": "0x20", "trust": "cfi" }
                ]
            },
            {
                "frames": [
                { "module_index": 1, "ip": "0x4010", "trust": "context" },
                { "module_index": 0, "ip": "0x30", "trust": "cfi" }
                ]
            }
            ]
        }
        """

        val stackTracesGlean = """
        {
            "crashType": "main",
            "crashAddress": "0xf001ba11",
            "crashThread": 1,
            "mainModule": 0,
            "modules": [
            {
                "baseAddress": "0x00000000",
                "endAddress": "0x00004000",
                "codeId": "8675309",
                "debugFile": "",
                "debugId": "18675309",
                "filename": "foo.exe",
                "version": "1.0.0"
            },
            {
                "baseAddress": "0x00004000",
                "endAddress": "0x00008000",
                "codeId": "42",
                "debugFile": "foo.pdb",
                "debugId": "43",
                "filename": "foo.dll",
                "version": "1.1.0"
            }
            ],
            "threads": [
            {
                "frames": [
                { "moduleIndex": 0, "ip": "0x10", "trust": "context" },
                { "moduleIndex": 0, "ip": "0x20", "trust": "cfi" }
                ]
            },
            {
                "frames": [
                { "moduleIndex": 1, "ip": "0x4010", "trust": "context" },
                { "moduleIndex": 0, "ip": "0x30", "trust": "cfi" }
                ]
            }
            ]
        }
        """

        val extrasFile = tempFolder.newFile()
        extrasFile.writeText(
            """
            {
                "ReleaseChannel": "beta",
                "Version": "123.0.0",
                "StartupCrash": "1",
                "TotalPhysicalMemory": 100,
                "AsyncShutdownTimeout": "{\"phase\":\"abcd\",\"conditions\":[{\"foo\":\"bar\"}],\"brokenAddBlockers\":[\"foo\"]}",
                "QuotaManagerShutdownTimeout": "line1\nline2\nline3",
                "StackTraces": $stackTracesAnnotation
            }
            """.trimIndent(),
        )

        val crash = Crash.NativeCodeCrash(
            12340000,
            "",
            extrasFile.path,
            Crash.NativeCodeCrash.PROCESS_TYPE_MAIN,
            breadcrumbs = arrayListOf(),
            remoteType = null,
        )

        service.record(crash)

        assertTrue("Persistence file must exist", service.file.exists())

        run {
            var pingReceived = false
            GleanPings.crash.testBeforeNextSubmit { _ ->
                val date = GregorianCalendar().apply {
                    time = Date(12340000)
                }
                date.set(Calendar.SECOND, 0)
                date.set(Calendar.MILLISECOND, 0)
                assertEquals(date.time, GleanCrash.time.testGetValue())
                assertEquals(0L, GleanEnvironment.uptime.testGetValue())
                assertEquals("main", GleanCrash.processType.testGetValue())
                assertEquals(true, GleanCrash.startup.testGetValue())
                assertEquals("os_fault", GleanCrash.cause.testGetValue())
                assertNull(GleanCrash.remoteType.testGetValue())
                assertEquals("beta", GleanCrash.appChannel.testGetValue())
                assertEquals("123.0.0", GleanCrash.appDisplayVersion.testGetValue())
                assertEquals(100L, GleanMemory.totalPhysical.testGetValue())
                assertEquals(
                    JsonObject(
                        mapOf(
                            "phase" to JsonPrimitive("abcd"),
                            "conditions" to JsonPrimitive("[{\"foo\":\"bar\"}]"),
                            "brokenAddBlockers" to JsonArray(listOf(JsonPrimitive("foo"))),
                        ),
                    ),
                    GleanCrash.asyncShutdownTimeout.testGetValue(),
                )
                assertEquals(
                    JsonArray(
                        listOf(
                            "line1",
                            "line2",
                            "line3",
                        ).map { e -> JsonPrimitive(e) },
                    ),
                    GleanCrash.quotaManagerShutdownTimeout.testGetValue(),
                )
                assertEquals(
                    Json.decodeFromString<JsonObject>(stackTracesGlean),
                    GleanCrash.stackTraces.testGetValue(),
                )
                pingReceived = true
            }

            GleanCrashReporterService(context)
            assertTrue("Expected ping to be sent", pingReceived)
        }
    }

    @Test
    fun `GleanCrashReporterService sends exception crash pings`() {
        val service = spy(GleanCrashReporterService(context))

        val crash = Crash.UncaughtExceptionCrash(
            12340000,
            RuntimeException("Test", java.io.IOException("IO")),
            arrayListOf(),
        )

        service.record(crash)

        assertTrue("Persistence file must exist", service.file.exists())

        val lines = service.file.readLines()
        assertEquals(
            "First element must be uncaught exception",
            crashCountJson(GleanCrashReporterService.UNCAUGHT_EXCEPTION_KEY),
            lines[0],
        )
        assertTrue(
            "Second element must be uncaught exception crash ping",
            exceptionPingJson(0, 12340000) matches lines[1],
        )

        run {
            var pingReceived = false
            GleanPings.crash.testBeforeNextSubmit { _ ->
                val date = GregorianCalendar().apply {
                    time = Date(12340000)
                }
                date.set(Calendar.SECOND, 0)
                date.set(Calendar.MILLISECOND, 0)
                assertEquals(date.time, GleanCrash.time.testGetValue())
                assertEquals(0L, GleanEnvironment.uptime.testGetValue())
                assertEquals("main", GleanCrash.processType.testGetValue())
                assertEquals(false, GleanCrash.startup.testGetValue())
                assertEquals("java_exception", GleanCrash.cause.testGetValue())
                val exc = GleanCrash.javaException.testGetValue()
                assertNotNull(exc)
                val throwables = exc?.jsonObject?.get("throwables")
                assertNotNull(throwables)
                throwables?.jsonArray?.let { arr ->
                    assertEquals(2, arr.size)
                    val first = arr.get(0).jsonObject.toMutableMap()
                    assertNotNull(first.remove("stack"))
                    assertEquals(
                        JsonObject(
                            mapOf(
                                "typeName" to JsonPrimitive("java.lang.RuntimeException"),
                                "message" to JsonPrimitive("Test"),
                            ),
                        ),
                        first,
                    )
                    assertEquals(
                        JsonObject(
                            mapOf(
                                "typeName" to JsonPrimitive("java.io.IOException"),
                                "message" to JsonPrimitive("IO"),
                            ),
                        ),
                        arr.get(1),
                    )
                }
                pingReceived = true
            }

            GleanCrashReporterService(context)
            assertTrue("Expected ping to be sent", pingReceived)
        }
    }
}
