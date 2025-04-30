/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.settings.biometric

import android.os.Build.VERSION_CODES.LOLLIPOP_MR1
import android.os.Build.VERSION_CODES.M
import androidx.biometric.BiometricManager
import androidx.biometric.BiometricManager.Authenticators.BIOMETRIC_WEAK
import androidx.biometric.BiometricManager.Authenticators.DEVICE_CREDENTIAL
import androidx.biometric.BiometricManager.BIOMETRIC_ERROR_HW_UNAVAILABLE
import androidx.biometric.BiometricManager.BIOMETRIC_SUCCESS
import androidx.biometric.BiometricPrompt
import androidx.fragment.app.Fragment
import io.mockk.every
import io.mockk.mockk
import io.mockk.slot
import io.mockk.verify
import mozilla.components.support.test.robolectric.createAddedTestFragment
import mozilla.components.support.test.robolectric.testContext
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import org.mozilla.fenix.helpers.FenixRobolectricTestRunner
import org.mozilla.fenix.settings.biometric.ext.isEnrolled
import org.mozilla.fenix.settings.biometric.ext.isHardwareAvailable
import org.robolectric.annotation.Config

@RunWith(FenixRobolectricTestRunner::class)
class BiometricPromptFeatureTest {

    lateinit var fragment: Fragment
    lateinit var manager: BiometricManager

    @Before
    fun setup() {
        fragment = createAddedTestFragment { Fragment() }
        manager = mockk()
    }

    @Config(sdk = [LOLLIPOP_MR1])
    @Test
    fun `canUseFeature checks for SDK compatible`() {
        assertFalse(BiometricPromptFeature.canUseFeature(manager))
    }

    @Config(sdk = [M])
    @Test
    fun `canUseFeature checks for hardware capabilities`() {
        every { manager.canAuthenticate(any()) } returns BIOMETRIC_SUCCESS

        assertTrue(BiometricPromptFeature.canUseFeature(manager))

        every { manager.canAuthenticate(any()) } returns BIOMETRIC_ERROR_HW_UNAVAILABLE

        assertFalse(BiometricPromptFeature.canUseFeature(manager))

        verify { manager.isEnrolled() }
        verify { manager.isHardwareAvailable() }
    }

    @Test
    fun `prompt is created and destroyed on start and stop`() {
        val feature = BiometricPromptFeature(testContext, fragment, {}, {})

        assertNull(feature.biometricPrompt)

        feature.start()

        assertNotNull(feature.biometricPrompt)

        feature.stop()

        assertNull(feature.biometricPrompt)
    }

    @Test
    fun `requestAuthentication invokes biometric prompt`() {
        val feature = BiometricPromptFeature(testContext, fragment, {}, {})
        val prompt: BiometricPrompt = mockk(relaxed = true)
        val promptInfo = slot<BiometricPrompt.PromptInfo>()

        feature.biometricPrompt = prompt

        feature.requestAuthentication("test")

        verify { prompt.authenticate(capture(promptInfo)) }
        assertEquals(BIOMETRIC_WEAK or DEVICE_CREDENTIAL, promptInfo.captured.allowedAuthenticators)
        assertEquals("test", promptInfo.captured.title)
    }

    @Test
    fun `promptCallback fires feature callbacks`() {
        val authSuccess: () -> Unit = mockk(relaxed = true)
        val authFailure: () -> Unit = mockk(relaxed = true)
        val feature = BiometricPromptFeature(testContext, fragment, authFailure, authSuccess)
        val callback = feature.PromptCallback()
        val prompt = BiometricPrompt(fragment, callback)

        feature.biometricPrompt = prompt

        callback.onAuthenticationError(0, "")

        verify { authFailure.invoke() }

        callback.onAuthenticationFailed()

        verify { authFailure.invoke() }

        callback.onAuthenticationSucceeded(mockk())

        verify { authSuccess.invoke() }
    }
}
