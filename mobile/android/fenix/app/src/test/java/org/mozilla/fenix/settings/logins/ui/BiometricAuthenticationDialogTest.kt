/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.settings.logins.ui

import android.content.Context
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.test.junit4.v2.createComposeRule
import androidx.fragment.app.FragmentActivity
import androidx.test.ext.junit.runners.AndroidJUnit4
import org.junit.Assert.assertEquals
import org.junit.Before
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith
import org.mozilla.fenix.R
import org.mozilla.fenix.settings.biometric.AutomotiveSensitiveDataAuthenticator
import org.robolectric.Robolectric

@RunWith(AndroidJUnit4::class)
class BiometricAuthenticationDialogTest {
    @get:Rule val composeTestRule = createComposeRule()

    private lateinit var activity: FragmentActivity
    private var successCount = 0
    private var failureCount = 0

    @Before
    fun setup() {
        val controller = Robolectric.buildActivity(FragmentActivity::class.java)
        controller.get().setTheme(R.style.NormalTheme)
        activity = controller.setup().get()
    }

    private class FakeAuthenticator(private val isRequired: Boolean) : AutomotiveSensitiveDataAuthenticator {
        val titles = mutableListOf<String>()
        var onAuthenticated: () -> Unit = {}
        var onBlocked: () -> Unit = {}

        override fun isRequired(context: Context) = isRequired

        override fun authenticate(
            activity: FragmentActivity,
            title: String,
            onAuthenticated: () -> Unit,
            onBlocked: () -> Unit,
        ) {
            titles.add(title)
            this.onAuthenticated = onAuthenticated
            this.onBlocked = onBlocked
        }
    }

    private fun setContent(authenticator: AutomotiveSensitiveDataAuthenticator) {
        composeTestRule.setContent {
            CompositionLocalProvider(LocalContext provides activity) {
                BiometricAuthenticationDialog(
                    title = "title",
                    onAuthSuccess = { successCount++ },
                    onAuthFailure = { failureCount++ },
                    automotiveAuthenticator = authenticator,
                )
            }
        }
        composeTestRule.waitForIdle()
    }

    @Test
    fun `GIVEN the automotive check is required WHEN shown THEN the automotive authentication is started`() {
        val authenticator = FakeAuthenticator(isRequired = true)

        setContent(authenticator)

        assertEquals(listOf("title"), authenticator.titles)
    }

    @Test
    fun `GIVEN the automotive check is required WHEN it succeeds THEN onAuthSuccess is invoked`() {
        val authenticator = FakeAuthenticator(isRequired = true)
        setContent(authenticator)

        authenticator.onAuthenticated()

        assertEquals(1, successCount)
        assertEquals(0, failureCount)
    }

    @Test
    fun `GIVEN the automotive check is required WHEN it is blocked THEN onAuthFailure is invoked`() {
        val authenticator = FakeAuthenticator(isRequired = true)
        setContent(authenticator)

        authenticator.onBlocked()

        assertEquals(0, successCount)
        assertEquals(1, failureCount)
    }

    @Test
    fun `GIVEN the automotive check is required WHEN recomposed THEN the automotive authentication is started only once`() {
        val authenticator = FakeAuthenticator(isRequired = true)
        val recomposeTrigger = mutableIntStateOf(0)
        composeTestRule.setContent {
            CompositionLocalProvider(LocalContext provides activity) {
                recomposeTrigger.intValue
                BiometricAuthenticationDialog(
                    title = "title",
                    onAuthSuccess = { successCount++ },
                    onAuthFailure = { failureCount++ },
                    automotiveAuthenticator = authenticator,
                )
            }
        }
        composeTestRule.waitForIdle()

        recomposeTrigger.intValue++
        composeTestRule.waitForIdle()

        assertEquals(1, authenticator.titles.size)
    }

    @Test
    fun `GIVEN the automotive check is not required WHEN shown THEN the automotive authentication is not started`() {
        val authenticator = FakeAuthenticator(isRequired = false)

        setContent(authenticator)

        assertEquals(emptyList<String>(), authenticator.titles)
    }
}
