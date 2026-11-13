/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.settings.biometric

import android.app.KeyguardManager
import android.content.Context
import android.os.Bundle
import androidx.fragment.app.FragmentActivity
import androidx.preference.Preference
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import org.mozilla.fenix.R
import org.mozilla.fenix.settings.requirePreference
import org.robolectric.Robolectric
import org.robolectric.RobolectricTestRunner

@RunWith(RobolectricTestRunner::class)
class BiometricPromptPreferenceFragmentTest {

    private lateinit var activity: FragmentActivity
    private lateinit var fragment: TestFragment
    private lateinit var authenticator: FakeAuthenticator
    private val prefList = listOf(R.string.pref_key_credit_cards_manage_cards)

    @Before
    fun setup() {
        val controller = Robolectric.buildActivity(FragmentActivity::class.java)
        controller.get().setTheme(R.style.NormalTheme)
        activity = controller.setup().get()
        fragment = TestFragment()
        activity.supportFragmentManager.beginTransaction().add(fragment, "test").commitNow()
    }

    private fun verify(isRequired: Boolean) {
        authenticator = FakeAuthenticator(isRequired)
        fragment.automotiveAuthenticator = authenticator
        fragment.verifyCredentialsOrShowSetupWarning(activity, prefList)
    }

    private fun isPrefEnabled() = fragment.requirePreference<Preference>(prefList.first()).isEnabled

    @Test
    fun `GIVEN the automotive check is required WHEN verifying credentials THEN it authenticates with the unlock message and disables the preferences`() {
        verify(isRequired = true)

        assertEquals(listOf(activity), authenticator.activities)
        assertEquals(listOf(UNLOCK_MESSAGE), authenticator.titles)
        assertFalse(isPrefEnabled())
        assertEquals(0, fragment.navigateOnSuccessCount)
        assertEquals(0, fragment.pinDialogWarningCount)
    }

    @Test
    fun `GIVEN the automotive check is required WHEN authentication succeeds THEN it navigates`() {
        verify(isRequired = true)

        authenticator.onAuthenticated()

        assertEquals(1, fragment.navigateOnSuccessCount)
    }

    @Test
    fun `GIVEN the automotive check is required WHEN authentication is blocked THEN it does not navigate and re-enables the preferences`() {
        verify(isRequired = true)

        authenticator.onBlocked()

        assertEquals(0, fragment.navigateOnSuccessCount)
        assertTrue(isPrefEnabled())
    }

    @Test
    fun `GIVEN the automotive check is required WHEN the fragment is removed before the result THEN nothing happens`() {
        verify(isRequired = true)
        activity.supportFragmentManager.beginTransaction().remove(fragment).commitNow()

        authenticator.onAuthenticated()
        authenticator.onBlocked()

        assertEquals(0, fragment.navigateOnSuccessCount)
    }

    @Test
    fun `GIVEN the automotive check is not required WHEN verifying credentials THEN the automotive authentication is not used`() {
        verify(isRequired = false)

        assertEquals(emptyList<String>(), authenticator.titles)
    }

    internal class TestFragment : BiometricPromptPreferenceFragment() {
        var navigateOnSuccessCount = 0
        var pinDialogWarningCount = 0

        override fun onCreatePreferences(savedInstanceState: Bundle?, rootKey: String?) {
            preferenceScreen =
                preferenceManager.createPreferenceScreen(requireContext()).apply {
                    addPreference(
                        Preference(requireContext()).apply {
                            key = getString(R.string.pref_key_credit_cards_manage_cards)
                        }
                    )
                }
        }

        override fun unlockMessage() = UNLOCK_MESSAGE

        override fun navigateOnSuccess() {
            navigateOnSuccessCount++
        }

        override fun showPinDialogWarning(context: Context) {
            pinDialogWarningCount++
        }

        override fun showPinVerification(manager: KeyguardManager) = Unit
    }

    private class FakeAuthenticator(private val isRequired: Boolean) : AutomotiveSensitiveDataAuthenticator {
        val activities = mutableListOf<FragmentActivity>()
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
            activities.add(activity)
            titles.add(title)
            this.onAuthenticated = onAuthenticated
            this.onBlocked = onBlocked
        }
    }

    private companion object {
        const val UNLOCK_MESSAGE = "unlock"
    }
}
