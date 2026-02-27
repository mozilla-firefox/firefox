/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.summarization.onboarding

import androidx.test.ext.junit.runners.AndroidJUnit4
import mozilla.components.support.test.robolectric.testContext
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import org.mozilla.fenix.utils.Settings

@RunWith(AndroidJUnit4::class)
class FenixSummarizationFeatureConfigurationTest {
    private lateinit var testSettings: Settings
    private lateinit var discoverySettings: FenixSummarizationFeatureConfiguration

    @Before
    fun setUp() {
        testSettings = Settings(
            appContext = testContext,
            packageName = "package-name",
            isBenchmarkBuild = false,
        )
        discoverySettings = FenixSummarizationFeatureConfiguration(
            settings = testSettings,
        )
    }

    @Test
    fun `menu item is visible if the feature flag is enabled and the feature itself is turned on`() {
        testSettings.shakeToSummarizeFeatureFlagEnabled = true
        testSettings.shakeToSummarizeFeatureUserPreference = true

        assertTrue(
            "Menu item should be visible if the feature flag is enabled and the feature itself is turned on",
            discoverySettings.showMenuItem,
        )
    }

    @Test
    fun `menu item is not visible if the feature flag is enabled and the feature itself is not enabled`() {
        // GIVEN the feature flag is enabled
        testSettings.shakeToSummarizeFeatureFlagEnabled = true

        // AND the feature is not enabled
        testSettings.shakeToSummarizeFeatureUserPreference = false

        assertFalse(
            "Menu item should not be visible if the feature flag is enabled and the feature itself is not enabled",
            discoverySettings.showMenuItem,
        )
    }

    @Test
    fun `menu item is not visible if the feature flag is not enabled`() {
        // GIVEN the feature flag is not enabled
        testSettings.shakeToSummarizeFeatureFlagEnabled = false

        // AND the feature is somehow left enabled
        testSettings.shakeToSummarizeFeatureUserPreference = true

        assertFalse(
            "Menu item should not be visible if the feature flag is not enabled",
            discoverySettings.showMenuItem,
        )
    }

    @Test
    fun `menu item is highlighted after menu item is exposed once`() {
        testSettings.shakeToSummarizeFeatureFlagEnabled = true
        testSettings.shakeToSummarizeFeatureUserPreference = true

        // menu item is exposed once
        discoverySettings.cacheDiscoveryEvent(SummarizeDiscoveryEvent.MenuItemExposure)

        assertTrue(
            "Menu item should be highlighted after menu item is exposed once",
            discoverySettings.shouldHighlightMenuItem,
        )
    }

    @Test
    fun `menu item is not highlighted after menu item is exposed the second time`() {
        // Given both the feature flag and feature are enabled
        testSettings.shakeToSummarizeFeatureFlagEnabled = true
        testSettings.shakeToSummarizeFeatureUserPreference = true

        // WHEN menu item is exposed twice
        discoverySettings.cacheDiscoveryEvent(SummarizeDiscoveryEvent.MenuItemExposure)
        discoverySettings.cacheDiscoveryEvent(SummarizeDiscoveryEvent.MenuItemExposure)

        assertFalse(
            "Menu item should be highlighted after menu item is exposed the second time",
            discoverySettings.shouldHighlightMenuItem,
        )
    }

    @Test
    fun `menu item is not highlighted if the feature is not enabled`() {
        // GIVEN the feature flag is enabled
        testSettings.shakeToSummarizeFeatureFlagEnabled = true

        // AND the feature is not enabled
        testSettings.shakeToSummarizeFeatureUserPreference = false

        assertFalse(
            "Menu item should not be highlighted if the feature is not enabled",
            discoverySettings.shouldHighlightMenuItem,
        )
    }

    @Test
    fun `menu item is not highlighted if the feature flag is not enabled`() {
        // GIVEN the feature flag is NOT enabled
        testSettings.shakeToSummarizeFeatureFlagEnabled = false

        // GIVEN the feature is somehow enabled
        testSettings.shakeToSummarizeFeatureUserPreference = true

        assertFalse(
            "Menu item should not be highlighted if the feature flag is not enabled",
            discoverySettings.shouldHighlightMenuItem,
        )
    }

    @Test
    fun `menu overflow is not highlighted if the feature flag is not enabled`() {
        // GIVEN the feature flag is NOT enabled
        testSettings.shakeToSummarizeFeatureFlagEnabled = false

        // GIVEN the feature is somehow enabled
        testSettings.shakeToSummarizeFeatureUserPreference = true

        assertFalse(
            "Menu overflow should not be highlighted if the feature flag is not enabled",
            discoverySettings.shouldHighlightOverflowMenuItem,
        )
    }

    @Test
    fun `menu overflow is not highlighted if the feature is not enabled`() {
        // GIVEN the feature flag is enabled
        testSettings.shakeToSummarizeFeatureFlagEnabled = true

        // GIVEN the feature is NOT enabled
        testSettings.shakeToSummarizeFeatureUserPreference = false

        assertFalse(
            "Menu overflow should not be highlighted if the feature is not enabled",
            discoverySettings.shouldHighlightOverflowMenuItem,
        )
    }

    @Test
    fun `menu overflow is highlighted if the overflow menu item is not interacted with at all`() {
        // GIVEN the feature flag fully available
        testSettings.shakeToSummarizeFeatureFlagEnabled = true
        testSettings.shakeToSummarizeFeatureUserPreference = true

        assertTrue(
            "Menu overflow should be highlighted after the overflow menu item is not interacted with",
            discoverySettings.shouldHighlightOverflowMenuItem,
        )
    }

    @Test
    fun `menu overflow is not highlighted after the overflow menu item is interacted with once`() {
        // GIVEN the feature flag fully available
        testSettings.shakeToSummarizeFeatureFlagEnabled = true
        testSettings.shakeToSummarizeFeatureUserPreference = true

        // WHEN the overflow menu item is interacted with
        discoverySettings.cacheDiscoveryEvent(SummarizeDiscoveryEvent.MenuOverflowInteraction)

        assertFalse(
            "Menu overflow should not be highlighted after the overflow menu item has been interacted with",
            discoverySettings.shouldHighlightOverflowMenuItem,
        )
    }

    @Test
    fun `menu overflow interaction does not get recorded if it is already more than one time`() {
        // GIVEN the feature flag fully available
        testSettings.shakeToSummarizeFeatureFlagEnabled = true
        testSettings.shakeToSummarizeFeatureUserPreference = true

        // WHEN the overflow menu item is interacted with multiple times
        repeat(10) {
            discoverySettings.cacheDiscoveryEvent(SummarizeDiscoveryEvent.MenuOverflowInteraction)
        }

        // THEN the settings should be updated to the max count
        assertEquals(
            "Menu overflow interaction should not get recorded if it is already recorded",
            1,
            testSettings.shakeToSummarizeMoreMenuItemInteractionCount.value,
        )
    }

    @Test
    fun `menu item exposure does not get recorded if it is already more than two times`() {
        // GIVEN the feature flag fully available
        testSettings.shakeToSummarizeFeatureFlagEnabled = true
        testSettings.shakeToSummarizeFeatureUserPreference = true

        // WHEN the overflow menu item is interacted with multiple times
        repeat(10) {
            discoverySettings.cacheDiscoveryEvent(SummarizeDiscoveryEvent.MenuItemExposure)
        }

        // THEN the settings should be updated to the max count
        assertEquals(
            "Menu item exposure should not get recorded if it is already recorded twice",
            2,
            testSettings.shakeToSummarizeMenuItemExposureCount.value,
        )
    }

    @Test
    fun `toolbar overflow menu interaction does not get recorded if it is already more than once`() {
        // GIVEN the feature flag fully available
        testSettings.shakeToSummarizeFeatureFlagEnabled = true
        testSettings.shakeToSummarizeFeatureUserPreference = true

        // WHEN the toolbar overflow is interacted with multiple times
        repeat(10) {
            discoverySettings.cacheDiscoveryEvent(SummarizeDiscoveryEvent.ToolbarOverflowInteraction)
        }

        // THEN the settings should be updated to the max count
        assertEquals(
            "Menu item exposure should not get recorded if it is already recorded onnce",
            1,
            testSettings.shakeToSummarizeToolbarInteractionCount.value,
        )
    }
}
