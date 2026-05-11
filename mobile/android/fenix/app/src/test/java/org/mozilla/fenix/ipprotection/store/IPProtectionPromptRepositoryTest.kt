/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.ipprotection.store

import junit.framework.TestCase.assertFalse
import junit.framework.TestCase.assertTrue
import mozilla.components.support.test.robolectric.testContext
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import org.mozilla.fenix.utils.Settings
import org.mozilla.fenix.utils.Settings.Companion.ONE_WEEK_MS
import org.robolectric.RobolectricTestRunner

private const val CURRENT_TIME_MILLIS = 1759926358L

@RunWith(RobolectricTestRunner::class)
class IPProtectionPromptRepositoryTest {
    private lateinit var settings: Settings

    private lateinit var repository: DefaultIPProtectionPromptRepository

    @Before
    fun setup() {
        settings = Settings(testContext)
        repository = DefaultIPProtectionPromptRepository(
            settings = settings,
            installedTimeMillis = { CURRENT_TIME_MILLIS - ONE_WEEK_MS },
        )
    }

    @Test
    fun `WHEN all conditions satisfied THEN show the prompt`() {
        settings.isIPProtectionEnabled = true
        repository.isShowingPrompt = false

        assertTrue(settings.isIPProtectionAvailable)

        assertTrue(repository.canShowIPProtectionPrompt(CURRENT_TIME_MILLIS))
    }

    @Test
    fun `WHEN the prompt is already showing THEN do not show the prompt`() {
        settings.isIPProtectionEnabled = true
        repository.isShowingPrompt = true

        assertTrue(settings.isIPProtectionAvailable)

        assertFalse(repository.canShowIPProtectionPrompt(CURRENT_TIME_MILLIS))
    }

    @Test
    fun `WHEN the IP Protection feature is not available THEN do not show the prompt`() {
        settings.isIPProtectionEnabled = false
        repository.isShowingPrompt = false

        assertFalse(settings.isIPProtectionAvailable)

        assertFalse(repository.canShowIPProtectionPrompt(CURRENT_TIME_MILLIS))
    }

    @Test
    fun `WHEN the application was installed less than a week ago THEN do not show the prompt`() {
        repository = DefaultIPProtectionPromptRepository(
            settings = settings,
            installedTimeMillis = { CURRENT_TIME_MILLIS - (ONE_WEEK_MS - 1) },
        )
        settings.isIPProtectionEnabled = true
        repository.isShowingPrompt = false

        assertTrue(settings.isIPProtectionAvailable)

        assertFalse(repository.canShowIPProtectionPrompt(CURRENT_TIME_MILLIS))
    }

    @Test
    fun `WHEN the application was installed exactly a week ago THEN show the prompt`() {
        repository = DefaultIPProtectionPromptRepository(
            settings = settings,
            installedTimeMillis = { CURRENT_TIME_MILLIS - ONE_WEEK_MS },
        )
        settings.isIPProtectionEnabled = true
        repository.isShowingPrompt = false

        assertTrue(settings.isIPProtectionAvailable)

        assertTrue(repository.canShowIPProtectionPrompt(CURRENT_TIME_MILLIS))
    }

    @Test
    fun `WHEN the application was installed over a week ago THEN show the prompt`() {
        repository = DefaultIPProtectionPromptRepository(
            settings = settings,
            installedTimeMillis = { CURRENT_TIME_MILLIS - (ONE_WEEK_MS + 1) },
        )
        settings.isIPProtectionEnabled = true
        repository.isShowingPrompt = false

        assertTrue(settings.isIPProtectionAvailable)

        assertTrue(repository.canShowIPProtectionPrompt(CURRENT_TIME_MILLIS))
    }
}
