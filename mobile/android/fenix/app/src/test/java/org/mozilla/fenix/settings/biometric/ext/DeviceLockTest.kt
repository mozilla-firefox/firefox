/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.settings.biometric.ext

import android.app.KeyguardManager
import androidx.core.content.getSystemService
import mozilla.components.support.test.robolectric.testContext
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.Shadows.shadowOf

@RunWith(RobolectricTestRunner::class)
class DeviceLockTest {

    private fun setKeyguardSecure(isSecure: Boolean) {
        shadowOf(testContext.getSystemService<KeyguardManager>()).setIsKeyguardSecure(isSecure)
    }

    @Test
    fun `GIVEN a device lock is set up THEN isDeviceLockSetUp is true`() {
        setKeyguardSecure(true)

        assertTrue(testContext.isDeviceLockSetUp())
    }

    @Test
    fun `GIVEN no device lock is set up THEN isDeviceLockSetUp is false`() {
        setKeyguardSecure(false)

        assertFalse(testContext.isDeviceLockSetUp())
    }
}
