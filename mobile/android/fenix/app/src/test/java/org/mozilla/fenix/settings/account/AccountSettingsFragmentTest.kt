/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.settings.account

import androidx.lifecycle.Lifecycle
import androidx.lifecycle.LifecycleOwner
import androidx.lifecycle.LifecycleRegistry
import androidx.preference.EditTextPreference
import androidx.preference.Preference
import androidx.preference.PreferenceCategory
import androidx.preference.SwitchPreferenceCompat
import io.mockk.every
import io.mockk.mockk
import io.mockk.spyk
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertTrue
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.test.StandardTestDispatcher
import kotlinx.coroutines.test.resetMain
import kotlinx.coroutines.test.runCurrent
import kotlinx.coroutines.test.runTest
import kotlinx.coroutines.test.setMain
import mozilla.components.concept.sync.AccountObserver
import mozilla.components.service.fxa.sync.SyncStatusObserver
import mozilla.components.support.test.robolectric.testContext
import org.junit.After
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import org.mozilla.fenix.R
import org.mozilla.fenix.ext.components
import org.robolectric.RobolectricTestRunner
import org.robolectric.util.ReflectionHelpers

@OptIn(ExperimentalCoroutinesApi::class)
@Suppress("NoDispatchersSetMainInTests") // Lifecycle scopes use Dispatchers.Main.
@RunWith(RobolectricTestRunner::class)
class AccountSettingsFragmentTest {
    private val dispatcher = StandardTestDispatcher()
    private lateinit var fragment: AccountSettingsFragment
    private lateinit var lifecycle: LifecycleRegistry
    private lateinit var preferences: Map<String, Preference>

    @Before
    fun setUp() {
        Dispatchers.setMain(dispatcher)
        val owner = mockk<LifecycleOwner>()
        lifecycle = LifecycleRegistry.createUnsafe(owner)
        every { owner.lifecycle } returns lifecycle
        lifecycle.currentState = Lifecycle.State.RESUMED
        every { testContext.components.settings.accountSettingsNewUi } returns true
        every { testContext.components.backgroundServices.accountManager.accountNeedsReauth() } returns true

        preferences = buildMap {
            put(testContext.getString(R.string.pref_key_sync_now), Preference(testContext))
            put(testContext.getString(R.string.pref_key_sync_enable_sync), SwitchPreferenceCompat(testContext))
            put(testContext.getString(R.string.preferences_sync_category), PreferenceCategory(testContext))
            put(testContext.getString(R.string.pref_key_sync_device_name), EditTextPreference(testContext))
            put(
                testContext.getString(R.string.pref_key_account_error_card_auth),
                AccountErrorPreference(testContext).apply { isVisible = false },
            )
            AccountSettingsFragment.SyncEngineUiData.entries.forEach {
                put(testContext.getString(it.prefId), SwitchPreferenceCompat(testContext))
            }
        }
        fragment = spyk()
        every { fragment.context } returns testContext
        every { fragment.viewLifecycleOwner } returns owner
        every { fragment.preferenceScreen } returns mockk(relaxed = true)
        every { fragment.findPreference<Preference>(any()) } answers { preferences[firstArg<String>()] }
        ReflectionHelpers.setField(fragment, "accountManager", testContext.components.backgroundServices.accountManager)
        ReflectionHelpers.setField(
            fragment,
            "accountSettingsStore",
            AccountSettingsFragmentStore(AccountSettingsFragmentState()),
        )
    }

    @After
    fun tearDown() {
        lifecycle.currentState = Lifecycle.State.DESTROYED
        Dispatchers.resetMain()
    }

    @Test
    fun `authentication failure ends syncing without a terminal sync callback`() =
        runTest(dispatcher) {
            val syncObserver = ReflectionHelpers.getField<SyncStatusObserver>(fragment, "syncStatusObserver")
            val accountObserver = ReflectionHelpers.getField<AccountObserver>(fragment, "accountStateObserver")
            val syncNow = preferences.getValue(testContext.getString(R.string.pref_key_sync_now))
            val engines = preferences.getValue(testContext.getString(R.string.preferences_sync_category))
            val deviceName = preferences.getValue(testContext.getString(R.string.pref_key_sync_device_name))

            syncObserver.onStarted()
            runCurrent()
            assertFalse(syncNow.isEnabled)
            assertTrue(engines.shouldDisableDependents())
            assertFalse(deviceName.isEnabled)

            accountObserver.onAuthenticationProblems()
            runCurrent()

            assertEquals(testContext.getString(R.string.preferences_sync_now), syncNow.title)
            assertTrue(syncNow.isEnabled)
            assertFalse(engines.shouldDisableDependents())
            assertTrue(deviceName.isEnabled)
            assertTrue(preferences.getValue(testContext.getString(R.string.pref_key_account_error_card_auth)).isVisible)
            assertFalse(engines.isVisible)
            val deviceSync = preferences.getValue(testContext.getString(R.string.pref_key_sync_enable_sync))
            assertFalse(deviceSync.isEnabled)
            assertFalse((deviceSync as SwitchPreferenceCompat).isChecked)
        }
}
