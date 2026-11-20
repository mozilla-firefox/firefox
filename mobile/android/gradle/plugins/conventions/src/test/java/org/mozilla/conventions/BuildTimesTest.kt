/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.conventions

import org.junit.jupiter.api.Assertions.*
import org.junit.jupiter.api.Test

class BuildTimesTest {
    @Test
    fun `task names map to their bucket`() {
        val expected = mapOf(
            ":app:compileDebugKotlin" to "kotlin",
            ":app:compileFenixNightlyKotlin" to "kotlin",
            ":app:compileDebugUnitTestKotlin" to "kotlin",
            ":components:feature-tabgroups-storage:kspDebugKotlin" to "ksp",
            ":app:compileDebugJavaWithJavac" to "java",
            ":app:minifyReleaseWithR8" to "r8",
            ":app:dexBuilderDebug" to "dex",
            ":app:mergeExtDexDebug" to "dex",
            ":app:mergeProjectDexDebug" to "dex",
            ":app:lintDebug" to "lint",
            ":components:support-utils:testDebugUnitTest" to "test",
            ":geckoview:machBuildFaster" to "mach",
            ":app:mergeDebugResources" to "resources",
            ":app:generateDebugResValues" to "resources",
            ":app:processDebugManifest" to "resources",
            ":app:mergeDebugAssets" to "resources",
            ":app:shrinkReleaseRes" to "resources",
            ":components:lib-crash:packageDebugResources" to "resources",
            ":app:generateSafeArgsDebug" to "codegen",
            ":app:generateDebugBuildConfig" to "codegen",
            ":app:packageDebug" to "packaging",
            ":app:assembleDebug" to "packaging",
            ":app:mergeDebugNativeLibs" to "packaging",
            ":app:stripDebugDebugSymbols" to "packaging",
            ":app:checkDebugAarMetadata" to "other",
            ":app:collectSomeResult" to "other",
            ":listRepositories" to "other",
            ":clean" to "other",
            ":conventions:compileKotlin" to "kotlin",
        )
        for ((path, bucket) in expected) {
            assertEquals(bucket, BuildTimes.bucketFor(path), path)
        }
    }

    @Test
    fun `suite reports the phases, the app compile tasks and the non zero task time sums`() {
        val summary = BuildSummary(
            invocationStartMs = 0L,
            invocationEndMs = 10_000L,
            configStartMs = 500L,
            executionStartMs = 2_500L,
            executionEndMs = 9_500L,
            tasks = listOf(
                TaskRecord(":app:compileDebugKotlin", 0L, 4000L, "EXECUTED"),
                TaskRecord(":app:kspDebugKotlin", 0L, 1500L, "EXECUTED"),
                TaskRecord(":components:browser-state:compileDebugKotlin", 0L, 2000L, "EXECUTED"),
                TaskRecord(":components:browser-state:mergeDebugResources", 0L, 250L, "UP-TO-DATE"),
                TaskRecord(":listRepositories", 0L, 10L, "EXECUTED"),
            ),
        )

        val data = BuildTimes.perfherderData(summary, listOf("fenix-debug", "taskcluster-c3d"))

        assertEquals(mapOf("name" to "build_metrics"), data["framework"])
        val suite = (data["suites"] as List<*>).single() as Map<*, *>
        assertEquals("gradle build times", suite["name"])
        assertEquals(10.0, suite["value"])
        assertEquals("s", suite["unit"])
        assertEquals(true, suite["lowerIsBetter"])
        assertEquals(false, suite["shouldAlert"])
        assertEquals(listOf("fenix-debug", "taskcluster-c3d"), suite["extraOptions"])

        val subtests = (suite["subtests"] as List<*>).associate { subtest ->
            val s = subtest as Map<*, *>
            s["name"] to s["value"]
        }
        assertEquals(
            listOf(
                "startup", "configuration", "execution",
                "app kotlin", "app ksp",
                "kotlin task time", "ksp task time", "resources task time", "other task time",
                "app task time", "components task time",
            ),
            subtests.keys.toList(),
        )
        assertEquals(0.5, subtests["startup"])
        assertEquals(2.0, subtests["configuration"])
        assertEquals(7.0, subtests["execution"])
        assertEquals(4.0, subtests["app kotlin"])
        assertEquals(1.5, subtests["app ksp"])
        assertEquals(6.0, subtests["kotlin task time"])
        assertEquals(1.5, subtests["ksp task time"])
        assertEquals(0.25, subtests["resources task time"])
        assertEquals(0.01, subtests["other task time"])
        assertEquals(5.5, subtests["app task time"])
        assertEquals(2.25, subtests["components task time"])
    }

    @Test
    fun `suite leaves out the phases a reused configuration cannot measure`() {
        val summary = BuildSummary(
            invocationStartMs = 0L,
            invocationEndMs = 100L,
            configStartMs = null,
            executionStartMs = 10L,
            executionEndMs = 90L,
            tasks = emptyList(),
        )

        val suite = (BuildTimes.perfherderData(summary, emptyList())["suites"] as List<*>).single() as Map<*, *>
        val names = (suite["subtests"] as List<*>).map { (it as Map<*, *>)["name"] }
        assertEquals(listOf("execution"), names)
    }
}
