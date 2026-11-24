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
import io.mockk.verify
import mozilla.components.feature.intent.processing.IntentProcessor
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
        every { activity.components.intentProcessors.authCustomTabIntentProcessor } returns
            mockk { coEvery { process(any()) } returns true }

        controller.create()
        shadowOf(Looper.getMainLooper()).idle()

        val startedIntent = shadowOf(activity).peekNextStartedActivity()
        assertEquals(AuthCustomTabActivity::class.java.name, startedIntent.component?.className)
        assertTrue(startedIntent.getBooleanExtra(AuthCustomTabActivity.EXTRA_OPEN_HOME_ON_AUTHENTICATED, false))
    }

    @Test
    fun `GIVEN normal browsing mode WHEN receiving an auth intent THEN the auth custom tab processor handles it`() {
        val intent = Intent(Intent.ACTION_VIEW, "https://accounts.firefox.com/pair".toUri())
        val controller = Robolectric.buildActivity(AuthIntentReceiverActivity::class.java, intent)
        val activity = controller.get()
        val customTabIntentProcessor: IntentProcessor = mockk()
        val privateCustomTabIntentProcessor: IntentProcessor = mockk()
        val authCustomTabIntentProcessor: IntentProcessor = mockk { every { process(any()) } returns true }
        val privateAuthCustomTabIntentProcessor: IntentProcessor = mockk()

        every { activity.components.settings.lastKnownMode.isPrivate } returns false
        every { activity.components.intentProcessors.customTabIntentProcessor } returns customTabIntentProcessor
        every { activity.components.intentProcessors.privateCustomTabIntentProcessor } returns
            privateCustomTabIntentProcessor
        every { activity.components.intentProcessors.authCustomTabIntentProcessor } returns authCustomTabIntentProcessor
        every { activity.components.intentProcessors.privateAuthCustomTabIntentProcessor } returns
            privateAuthCustomTabIntentProcessor

        controller.create()
        shadowOf(Looper.getMainLooper()).idle()

        verify { authCustomTabIntentProcessor.process(any()) }
        verify(exactly = 0) { customTabIntentProcessor.process(any()) }
        verify(exactly = 0) { privateCustomTabIntentProcessor.process(any()) }
        verify(exactly = 0) { privateAuthCustomTabIntentProcessor.process(any()) }
    }

    @Test
    fun `GIVEN private browsing mode WHEN receiving an auth intent THEN the private auth custom tab processor handles it`() {
        val intent = Intent(Intent.ACTION_VIEW, "https://accounts.firefox.com/pair".toUri())
        val controller = Robolectric.buildActivity(AuthIntentReceiverActivity::class.java, intent)
        val activity = controller.get()
        val customTabIntentProcessor: IntentProcessor = mockk()
        val privateCustomTabIntentProcessor: IntentProcessor = mockk()
        val authCustomTabIntentProcessor: IntentProcessor = mockk()
        val privateAuthCustomTabIntentProcessor: IntentProcessor = mockk { every { process(any()) } returns true }

        every { activity.components.settings.lastKnownMode.isPrivate } returns true
        every { activity.components.intentProcessors.customTabIntentProcessor } returns customTabIntentProcessor
        every { activity.components.intentProcessors.privateCustomTabIntentProcessor } returns
            privateCustomTabIntentProcessor
        every { activity.components.intentProcessors.authCustomTabIntentProcessor } returns authCustomTabIntentProcessor
        every { activity.components.intentProcessors.privateAuthCustomTabIntentProcessor } returns
            privateAuthCustomTabIntentProcessor

        controller.create()
        shadowOf(Looper.getMainLooper()).idle()

        verify { privateAuthCustomTabIntentProcessor.process(any()) }
        verify(exactly = 0) { customTabIntentProcessor.process(any()) }
        verify(exactly = 0) { privateCustomTabIntentProcessor.process(any()) }
        verify(exactly = 0) { authCustomTabIntentProcessor.process(any()) }
    }
}
