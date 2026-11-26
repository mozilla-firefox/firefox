/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.ui.efficiency.tools

import androidx.test.platform.app.InstrumentationRegistry
import org.junit.Test
import org.mozilla.fenix.ui.efficiency.helpers.BaseTest
import org.mozilla.fenix.ui.efficiency.navigation.LaunchConfig

/**
 * Launches the app under a given [LaunchConfig] and holds it there, so an emulator snapshot can be taken of a device
 * that is already running the app the way one group of tests expects.
 *
 * Not a test, and deliberately not in the `tests` package: the sweep's default suite is `ui.efficiency.tests`, so this
 * stays out of it while remaining dispatchable by name.
 *
 * It goes through BaseTest rather than writing preferences directly, because the thing worth capturing is the state the
 * real rule produces --- HomeActivityTestRule.beforeActivityLaunched applies the flags and then launches, and a
 * snapshot of anything else would be a snapshot of a situation no test is ever in.
 *
 * am instrument -w -e class ...tools.SnapshotPrimer#hold \ -e holdMs 45000 -e isPocketEnabled false ...
 *
 * Every LaunchConfig field can be overridden by an instrumentation argument of the same name; anything not passed keeps
 * the LaunchConfig default.
 */
class SnapshotPrimer : BaseTest() {

    private fun arg(name: String): String? = InstrumentationRegistry.getArguments().getString(name)

    private fun flag(name: String, fallback: Boolean): Boolean = arg(name)?.toBooleanStrictOrNull() ?: fallback

    override fun launchConfig(): LaunchConfig {
        val d = LaunchConfig()
        return LaunchConfig(
            skipOnboarding = flag("skipOnboarding", d.skipOnboarding),
            isPageLoadTranslationsPromptEnabled =
                flag("isPageLoadTranslationsPromptEnabled", d.isPageLoadTranslationsPromptEnabled),
            isPocketEnabled = flag("isPocketEnabled", d.isPocketEnabled),
            isRecentlyVisitedFeatureEnabled =
                flag("isRecentlyVisitedFeatureEnabled", d.isRecentlyVisitedFeatureEnabled),
            shouldUseExpandedToolbar = flag("shouldUseExpandedToolbar", d.shouldUseExpandedToolbar),
            isTabStripEnabled = flag("isTabStripEnabled", d.isTabStripEnabled),
        )
    }

    @Test
    fun hold() {
        val holdMs = arg("holdMs")?.toLongOrNull() ?: 40_000L
        on.home.navigateToPage()
        Thread.sleep(holdMs)
    }
}
