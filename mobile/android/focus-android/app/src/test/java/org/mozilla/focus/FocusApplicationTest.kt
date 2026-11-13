/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.focus

import android.os.UserManager
import androidx.test.core.app.ApplicationProvider
import androidx.work.testing.WorkManagerTestInitHelper
import org.junit.Assert.assertThrows
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.Shadows.shadowOf

@RunWith(RobolectricTestRunner::class)
class FocusApplicationTest {

    private val application: FocusApplication = ApplicationProvider.getApplicationContext()

    @Test
    fun `GIVEN the user is locked WHEN WorkManager initializes THEN it does not throw`() {
        shadowOf(application.getSystemService(UserManager::class.java)).setUserUnlocked(false)

        WorkManagerTestInitHelper.initializeTestWorkManager(application, application.workManagerConfiguration)
        WorkManagerTestInitHelper.closeWorkDatabase()
    }

    @Test
    fun `GIVEN the user is unlocked WHEN WorkManager fails to initialize THEN the error is rethrown`() {
        val handler = application.workManagerConfiguration.initializationExceptionHandler!!

        assertThrows(IllegalStateException::class.java) { handler.accept(IllegalStateException()) }
    }
}
