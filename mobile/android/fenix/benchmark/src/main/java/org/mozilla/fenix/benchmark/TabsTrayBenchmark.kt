/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.benchmark

import android.content.Intent
import android.os.Build
import androidx.annotation.RequiresApi
import androidx.benchmark.macro.BaselineProfileMode
import androidx.benchmark.macro.CompilationMode
import androidx.benchmark.macro.FrameTimingMetric
import androidx.benchmark.macro.MacrobenchmarkScope
import androidx.benchmark.macro.StartupTimingMetric
import androidx.benchmark.macro.junit4.MacrobenchmarkRule
import androidx.test.ext.junit.runners.AndroidJUnit4
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith
import org.mozilla.fenix.benchmark.utils.EXTRA_TAB_TRAY_ANIMATION
import org.mozilla.fenix.benchmark.utils.EXTRA_TAB_TRAY_ENHANCEMENTS
import org.mozilla.fenix.benchmark.utils.FENIX_HOME_DEEP_LINK
import org.mozilla.fenix.benchmark.utils.HtmlAsset
import org.mozilla.fenix.benchmark.utils.MockWebServerRule
import org.mozilla.fenix.benchmark.utils.TARGET_PACKAGE
import org.mozilla.fenix.benchmark.utils.enterSearchMode
import org.mozilla.fenix.benchmark.utils.loadSite
import org.mozilla.fenix.benchmark.utils.measureRepeatedDefault
import org.mozilla.fenix.benchmark.utils.openNewTabOnTabsTray
import org.mozilla.fenix.benchmark.utils.openTabsTray
import org.mozilla.fenix.benchmark.utils.switchTabs
import org.mozilla.fenix.benchmark.utils.url

/**
 * This test class benchmarks the speed of opening 2 new tabs and switching between them. Run this
 * benchmark to verify how effective a Baseline Profile is. It does this by comparing
 * [CompilationMode.None], which represents the app with no Baseline Profiles optimizations, and
 * [CompilationMode.Partial], which uses Baseline Profiles.
 *
 * Run this benchmark to see startup measurements and captured system traces for verifying
 * the effectiveness of your Baseline Profiles. You can run it directly from Android
 * Studio as an instrumentation test that logs the benchmark metrics with links to the Perfetto traces,
 *
 * or using the gradle command:
 *
 * ```
 * ./gradlew :benchmark:connectedBenchmarkAndroidTest -P android.testInstrumentationRunnerArguments.annotation=org.mozilla.fenix.benchmark.baselineprofile -P benchmarkTest -P disableOptimization
 * ```
 *
 * The metric results will be in `benchmark/build/outputs/connected_android_test_additional_output` folder.
 *
 * Run the benchmarks on a physical device, not an emulator because the emulator doesn't represent
 * real world performance and shares system resources with its host.
 *
 * For more information, see the [Macrobenchmark documentation](https://d.android.com/macrobenchmark#create-macrobenchmark)
 * and the [instrumentation arguments documentation](https://d.android.com/topic/performance/benchmarking/macrobenchmark-instrumentation-args).
 **/
@RunWith(AndroidJUnit4::class)
@TabsTrayMacrobenchmark
class TabsTrayBenchmark {
    @get:Rule
    val benchmarkRule = MacrobenchmarkRule()

    @get:Rule
    val mockRule = MockWebServerRule()

    @RequiresApi(Build.VERSION_CODES.Q)
    @Test
    fun switchTabsAnimationOn() =
        switchTabsBenchmark(
            compilationMode = CompilationMode.Partial(baselineProfileMode = BaselineProfileMode.Require),
            animationsEnabled = true,
        )

    @RequiresApi(Build.VERSION_CODES.Q)
    @Test
    fun switchTabsAnimationOff() =
        switchTabsBenchmark(
            compilationMode = CompilationMode.Partial(baselineProfileMode = BaselineProfileMode.Require),
            animationsEnabled = false,
        )

    @RequiresApi(Build.VERSION_CODES.Q)
    private fun switchTabsBenchmark(compilationMode: CompilationMode, animationsEnabled: Boolean) {
        var firstStart = true

        benchmarkRule.measureRepeatedDefault(
            packageName = TARGET_PACKAGE,
            metrics = listOf(
                StartupTimingMetric(),
                FrameTimingMetric()),
            compilationMode = compilationMode,
            setupBlock = {
                if (firstStart) {
                    prepareTabsTray(animationsEnabled)
                    firstStart = false
                }
            },
        ) {
            device.openTabsTray()
            device.switchTabs(siteName = HtmlAsset.SIMPLE.title, newTabUrl = mockRule.url(HtmlAsset.SIMPLE))

            device.openTabsTray()
            device.switchTabs(siteName = HtmlAsset.LONG.title, newTabUrl = mockRule.url(HtmlAsset.LONG))
        }
    }

    private fun MacrobenchmarkScope.prepareTabsTray(animationsEnabled: Boolean) {
        pressHome()
        val intent = Intent(Intent.ACTION_VIEW, FENIX_HOME_DEEP_LINK)
            .putExtra(EXTRA_TAB_TRAY_ENHANCEMENTS, true)
            .putExtra(EXTRA_TAB_TRAY_ANIMATION, animationsEnabled)

        intent.setPackage(packageName)
        startActivityAndWait(intent = intent)

        device.enterSearchMode()
        device.loadSite(url = mockRule.url(HtmlAsset.SIMPLE))

        device.openTabsTray()
        device.openNewTabOnTabsTray()
        device.enterSearchMode()
        device.loadSite(url = mockRule.url(HtmlAsset.LONG))
    }
}
