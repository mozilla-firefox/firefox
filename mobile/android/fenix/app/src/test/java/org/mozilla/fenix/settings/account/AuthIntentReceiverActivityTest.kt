/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.settings.account

import android.content.Intent
import android.os.Looper
import androidx.core.net.toUri
import io.mockk.coEvery
import io.mockk.every
import io.mockk.mockk
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import org.mozilla.fenix.ext.components
import org.robolectric.Robolectric
import org.robolectric.RobolectricTestRunner
import org.robolectric.Shadows.shadowOf

@RunWith(RobolectricTestRunner::class)
class AuthIntentReceiverActivityTest {

    @Test
    fun `WHEN forwarding to the auth custom tab THEN the open home extra is kept`() {
        val intent =
            Intent(Intent.ACTION_VIEW, "https://accounts.firefox.com/pair".toUri())
                .putExtra(AuthCustomTabActivity.EXTRA_OPEN_HOME_ON_AUTHENTICATED, true)
        val controller = Robolectric.buildActivity(AuthIntentReceiverActivity::class.java, intent)
        val activity = controller.get()
        every { activity.components.settings.lastKnownMode.isPrivate } returns false
        every { activity.components.intentProcessors.customTabIntentProcessor } returns
            mockk { coEvery { process(any()) } returns true }

        controller.create()
        shadowOf(Looper.getMainLooper()).idle()

        val startedIntent = shadowOf(activity).peekNextStartedActivity()
        assertEquals(AuthCustomTabActivity::class.java.name, startedIntent.component?.className)
        assertTrue(startedIntent.getBooleanExtra(AuthCustomTabActivity.EXTRA_OPEN_HOME_ON_AUTHENTICATED, false))
    }
}
