/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.autofill

import android.content.Context
import androidx.fragment.app.FragmentActivity
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertSame
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import org.mozilla.fenix.settings.biometric.AutomotiveSensitiveDataAuthenticator
import org.robolectric.Robolectric
import org.robolectric.RobolectricTestRunner

@RunWith(RobolectricTestRunner::class)
class AutofillUnlockActivityTest {

    private lateinit var activity: AutofillUnlockActivity
    private val authenticator = FakeAuthenticator()
    private var successCount = 0
    private var failureCount = 0

    @Before
    fun setup() {
        activity = Robolectric.buildActivity(AutofillUnlockActivity::class.java).get()
        activity.sensitiveDataAuthenticator = authenticator
    }

    @Test
    fun `GIVEN the automotive check does not apply WHEN authenticating THEN the default prompt is used`() {
        authenticator.isRequired = false

        val handled = activity.authenticate(onSuccess = { successCount++ }, onFailure = { failureCount++ })

        assertFalse(handled)
        assertNull(authenticator.activity)
        assertEquals(0, successCount)
        assertEquals(0, failureCount)
    }

    @Test
    fun `GIVEN the automotive check applies WHEN the user authenticates THEN the autofill service is unlocked`() {
        authenticator.isRequired = true

        val handled = activity.authenticate(onSuccess = { successCount++ }, onFailure = { failureCount++ })

        assertTrue(handled)
        assertSame(activity, authenticator.activity)
        assertEquals(0, successCount)

        authenticator.onAuthenticated()

        assertEquals(1, successCount)
        assertEquals(0, failureCount)
    }

    @Test
    fun `GIVEN the automotive check applies WHEN authentication is blocked THEN nothing is filled`() {
        authenticator.isRequired = true

        val handled = activity.authenticate(onSuccess = { successCount++ }, onFailure = { failureCount++ })

        assertTrue(handled)

        authenticator.onBlocked()

        assertEquals(0, successCount)
        assertEquals(1, failureCount)
    }

    private class FakeAuthenticator : AutomotiveSensitiveDataAuthenticator {
        var isRequired = false
        var activity: FragmentActivity? = null
        var onAuthenticated: () -> Unit = {}
        var onBlocked: () -> Unit = {}

        override fun isRequired(context: Context) = isRequired

        override fun authenticate(
            activity: FragmentActivity,
            title: String,
            onAuthenticated: () -> Unit,
            onBlocked: () -> Unit,
        ) {
            this.activity = activity
            this.onAuthenticated = onAuthenticated
            this.onBlocked = onBlocked
        }
    }
}
