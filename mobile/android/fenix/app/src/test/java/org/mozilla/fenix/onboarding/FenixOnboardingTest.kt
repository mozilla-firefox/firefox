/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.onboarding

import android.content.Context
import android.content.Context.MODE_PRIVATE
import android.content.SharedPreferences
import io.mockk.every
import io.mockk.mockk
import mozilla.components.support.test.robolectric.testContext
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import org.mozilla.fenix.ext.components
import org.mozilla.fenix.helpers.lifecycle.TestLifecycleOwner
import org.mozilla.fenix.helpers.perf.TestStrictModeManager
import org.mozilla.fenix.onboarding.FenixOnboarding.Companion.CURRENT_ONBOARDING_VERSION
import org.mozilla.fenix.onboarding.FenixOnboarding.Companion.LAST_VERSION_ONBOARDING_KEY
import org.mozilla.fenix.perf.StrictModeManager
import org.robolectric.RobolectricTestRunner

@RunWith(RobolectricTestRunner::class)
class FenixOnboardingTest {

    private lateinit var onboarding: FenixOnboarding
    private lateinit var preferences: SharedPreferences

    @Before
    fun setup() {
        preferences = testContext.getSharedPreferences("fenix.onboarding.test", MODE_PRIVATE)
        val context = mockk<Context>()
        every { context.components.strictMode } returns TestStrictModeManager() as StrictModeManager
        every { context.getSharedPreferences(any(), MODE_PRIVATE) } returns preferences

        onboarding = FenixOnboarding(context)
    }

    @Test
    fun testUserHasBeenOnboarded() {
        preferences.edit().putInt(LAST_VERSION_ONBOARDING_KEY, 0).apply()
        assertFalse(onboarding.userHasBeenOnboarded())

        preferences.edit().putInt(LAST_VERSION_ONBOARDING_KEY, CURRENT_ONBOARDING_VERSION).apply()
        assertTrue(onboarding.userHasBeenOnboarded())
    }

    @Test
    fun testFinish() {
        onboarding.finish()

        assertEquals(CURRENT_ONBOARDING_VERSION, preferences.getInt(LAST_VERSION_ONBOARDING_KEY, 0))
    }

    @Test
    fun `WHEN onboarding finishes THEN the onboarding completed listener is invoked once`() {
        val lifecycleOwner = TestLifecycleOwner()
        var onCompletedCallCount = 0

        onboarding.registerOnboardingCompletedListener(lifecycleOwner) { onCompletedCallCount++ }
        assertEquals(0, onCompletedCallCount)

        onboarding.finish()
        assertEquals(1, onCompletedCallCount)

        onboarding.finish()
        assertEquals(1, onCompletedCallCount)
    }

    @Test
    fun `WHEN an unrelated preference changes THEN the onboarding completed listener is not invoked`() {
        val lifecycleOwner = TestLifecycleOwner()
        var onCompletedCallCount = 0

        onboarding.registerOnboardingCompletedListener(lifecycleOwner) { onCompletedCallCount++ }
        preferences.edit().putBoolean("fenix.onboarding.unrelated", true).apply()

        assertEquals(0, onCompletedCallCount)
    }

    @Test
    fun `WHEN the version changes to one that is not current THEN the onboarding completed listener is not invoked`() {
        val lifecycleOwner = TestLifecycleOwner()
        var onCompletedCallCount = 0

        onboarding.registerOnboardingCompletedListener(lifecycleOwner) { onCompletedCallCount++ }
        preferences.edit().putInt(LAST_VERSION_ONBOARDING_KEY, CURRENT_ONBOARDING_VERSION + 1).apply()

        assertEquals(0, onCompletedCallCount)
    }

    @Test
    fun `GIVEN the owner is destroyed WHEN onboarding finishes THEN the onboarding completed listener is not invoked`() {
        val lifecycleOwner = TestLifecycleOwner()
        var onCompletedCallCount = 0

        onboarding.registerOnboardingCompletedListener(lifecycleOwner) { onCompletedCallCount++ }
        lifecycleOwner.onDestroy()
        onboarding.finish()

        assertEquals(0, onCompletedCallCount)
    }
}
