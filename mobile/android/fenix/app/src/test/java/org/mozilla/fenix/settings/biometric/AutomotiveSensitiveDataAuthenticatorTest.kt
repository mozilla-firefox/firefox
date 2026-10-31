/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.settings.biometric

import android.content.pm.PackageManager
import androidx.appcompat.app.AlertDialog
import androidx.biometric.BiometricPrompt
import androidx.fragment.app.FragmentActivity
import io.mockk.mockk
import mozilla.components.support.test.robolectric.testContext
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import org.mozilla.fenix.nimbus.AutomotiveSensitiveData
import org.mozilla.fenix.nimbus.FxNimbus
import org.robolectric.RobolectricTestRunner
import org.robolectric.Shadows.shadowOf

@RunWith(RobolectricTestRunner::class)
class AutomotiveSensitiveDataAuthenticatorTest {

    private val activity: FragmentActivity = mockk()
    private var authenticatedCount = 0
    private var blockedCount = 0
    private var setupPromptCount = 0
    private var authenticationPromptCount = 0
    private var onSetUp: () -> Unit = {}
    private var onSetupCancel: () -> Unit = {}
    private var onAuthenticationSuccess: () -> Unit = {}
    private var onAuthenticationFailure: () -> Unit = {}

    private val setupPrompt =
        object : AutomotiveAuthenticationSetupPrompt {
            override fun show(activity: FragmentActivity, onSetUp: () -> Unit, onCancel: () -> Unit): AlertDialog {
                setupPromptCount++
                this@AutomotiveSensitiveDataAuthenticatorTest.onSetUp = onSetUp
                onSetupCancel = onCancel
                return mockk(relaxed = true)
            }
        }

    @After
    fun tearDown() {
        FxNimbus.features.automotiveSensitiveData.withCachedValue(null)
    }

    private fun createAuthenticator(isRequired: Boolean = true, isDeviceLockSetUp: Boolean) =
        DefaultAutomotiveSensitiveDataAuthenticator(
            isRequired = { isRequired },
            isDeviceLockSetUp = { isDeviceLockSetUp },
            setupPrompt = setupPrompt,
            showAuthenticationPrompt = { _, _, onSuccess, onFailure ->
                authenticationPromptCount++
                onAuthenticationSuccess = onSuccess
                onAuthenticationFailure = onFailure
            },
        )

    private fun DefaultAutomotiveSensitiveDataAuthenticator.authenticate() =
        authenticate(
            activity = activity,
            title = "title",
            onAuthenticated = { authenticatedCount++ },
            onBlocked = { blockedCount++ },
        )

    private fun assertCounts(
        setupPrompt: Int = 0,
        authenticationPrompt: Int = 0,
        authenticated: Int = 0,
        blocked: Int = 0,
    ) {
        assertEquals(setupPrompt, setupPromptCount)
        assertEquals(authenticationPrompt, authenticationPromptCount)
        assertEquals(authenticated, authenticatedCount)
        assertEquals(blocked, blockedCount)
    }

    @Test
    fun `GIVEN the check is not required WHEN authenticating THEN it continues right away`() {
        createAuthenticator(isRequired = false, isDeviceLockSetUp = false).authenticate()

        assertCounts(authenticated = 1)
    }

    @Test
    fun `GIVEN a device lock is set up WHEN authenticating THEN the authentication prompt is shown`() {
        createAuthenticator(isDeviceLockSetUp = true).authenticate()

        assertCounts(authenticationPrompt = 1)
    }

    @Test
    fun `GIVEN a device lock is set up WHEN authentication succeeds THEN it continues`() {
        createAuthenticator(isDeviceLockSetUp = true).authenticate()

        onAuthenticationSuccess()

        assertCounts(authenticationPrompt = 1, authenticated = 1)
    }

    @Test
    fun `GIVEN a device lock is set up WHEN authentication fails THEN it is blocked`() {
        createAuthenticator(isDeviceLockSetUp = true).authenticate()

        onAuthenticationFailure()

        assertCounts(authenticationPrompt = 1, blocked = 1)
    }

    @Test
    fun `GIVEN no device lock is set up WHEN authenticating THEN the setup prompt is shown`() {
        createAuthenticator(isDeviceLockSetUp = false).authenticate()

        assertCounts(setupPrompt = 1)
    }

    @Test
    fun `GIVEN no device lock is set up WHEN the setup prompt is cancelled THEN it is blocked without authenticating`() {
        createAuthenticator(isDeviceLockSetUp = false).authenticate()

        onSetupCancel()

        assertCounts(setupPrompt = 1, blocked = 1)
    }

    @Test
    fun `GIVEN no device lock is set up WHEN the user sets one up THEN the authentication prompt is shown`() {
        createAuthenticator(isDeviceLockSetUp = false).authenticate()

        onSetUp()

        assertCounts(setupPrompt = 1, authenticationPrompt = 1)
    }

    @Test
    fun `GIVEN the user set up a device lock WHEN authentication succeeds THEN it continues`() {
        createAuthenticator(isDeviceLockSetUp = false).authenticate()
        onSetUp()

        onAuthenticationSuccess()

        assertCounts(setupPrompt = 1, authenticationPrompt = 1, authenticated = 1)
    }

    @Test
    fun `GIVEN the user set up a device lock WHEN authentication fails THEN it is blocked`() {
        createAuthenticator(isDeviceLockSetUp = false).authenticate()
        onSetUp()

        onAuthenticationFailure()

        assertCounts(setupPrompt = 1, authenticationPrompt = 1, blocked = 1)
    }

    @Test
    fun `GIVEN no device lock is set up WHEN authenticating again THEN the setup prompt is shown again`() {
        val authenticator = createAuthenticator(isDeviceLockSetUp = false)
        authenticator.authenticate()
        onSetupCancel()

        authenticator.authenticate()

        assertCounts(setupPrompt = 2, blocked = 1)
    }

    @Test
    fun `GIVEN a device lock is set up WHEN authenticating again THEN the authentication prompt is shown again`() {
        val authenticator = createAuthenticator(isDeviceLockSetUp = true)
        authenticator.authenticate()
        onAuthenticationSuccess()

        authenticator.authenticate()

        assertCounts(authenticationPrompt = 2, authenticated = 1)
    }

    @Test
    fun `GIVEN Android Automotive and the feature is enabled THEN the check is required`() {
        shadowOf(testContext.packageManager).setSystemFeature(PackageManager.FEATURE_AUTOMOTIVE, true)
        FxNimbus.features.automotiveSensitiveData.withCachedValue(AutomotiveSensitiveData(enabled = true))

        assertTrue(isAutomotiveSensitiveDataAuthenticationRequired(testContext))
    }

    @Test
    fun `GIVEN Android Automotive and the feature is disabled THEN the check is not required`() {
        shadowOf(testContext.packageManager).setSystemFeature(PackageManager.FEATURE_AUTOMOTIVE, true)
        FxNimbus.features.automotiveSensitiveData.withCachedValue(AutomotiveSensitiveData(enabled = false))

        assertFalse(isAutomotiveSensitiveDataAuthenticationRequired(testContext))
    }

    @Test
    fun `GIVEN not Android Automotive and the feature is enabled THEN the check is not required`() {
        shadowOf(testContext.packageManager).setSystemFeature(PackageManager.FEATURE_AUTOMOTIVE, false)
        FxNimbus.features.automotiveSensitiveData.withCachedValue(AutomotiveSensitiveData(enabled = true))

        assertFalse(isAutomotiveSensitiveDataAuthenticationRequired(testContext))
    }

    @Test
    fun `WHEN authentication succeeds THEN the callback reports success`() {
        var successCount = 0
        var failureCount = 0
        val callback = DeviceLockAuthenticationCallback({ successCount++ }, { failureCount++ })

        callback.onAuthenticationSucceeded(mockk<BiometricPrompt.AuthenticationResult>())

        assertEquals(1, successCount)
        assertEquals(0, failureCount)
    }

    @Test
    fun `WHEN authentication errors or is cancelled THEN the callback reports failure`() {
        var successCount = 0
        var failureCount = 0
        val callback = DeviceLockAuthenticationCallback({ successCount++ }, { failureCount++ })

        callback.onAuthenticationError(BiometricPrompt.ERROR_USER_CANCELED, "cancelled")

        assertEquals(0, successCount)
        assertEquals(1, failureCount)
    }

    @Test
    fun `WHEN a single attempt fails THEN the callback reports nothing as the user can try again`() {
        var successCount = 0
        var failureCount = 0
        val callback = DeviceLockAuthenticationCallback({ successCount++ }, { failureCount++ })

        callback.onAuthenticationFailed()

        assertEquals(0, successCount)
        assertEquals(0, failureCount)
    }
}
