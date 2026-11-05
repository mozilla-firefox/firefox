/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.settings.autofill

import android.content.Context
import androidx.fragment.app.FragmentActivity
import androidx.navigation.NavController
import androidx.preference.Preference
import androidx.preference.SwitchPreferenceCompat
import io.mockk.every
import io.mockk.mockk
import io.mockk.verify
import kotlin.test.assertNotNull
import kotlinx.coroutines.test.runTest
import mozilla.components.concept.storage.Address
import mozilla.components.concept.storage.CreditCard
import mozilla.components.support.test.robolectric.testContext
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import org.mozilla.fenix.R
import org.mozilla.fenix.ext.components
import org.mozilla.fenix.ext.getPreferenceKey
import org.mozilla.fenix.settings.biometric.AutomotiveSensitiveDataAuthenticator
import org.mozilla.fenix.settings.requirePreference
import org.robolectric.Robolectric
import org.robolectric.RobolectricTestRunner

@RunWith(RobolectricTestRunner::class)
class AutofillSettingFragmentTest {
    private lateinit var autofillSettingFragment: AutofillSettingFragment
    private val navController: NavController = mockk(relaxed = true)

    @Before
    fun setUp() {
        every { testContext.components.settings } returns mockk(relaxed = true)
        every { testContext.components.core } returns mockk(relaxed = true)

        every { testContext.components.settings.enableComposeAutofillSettings } returns false
        every { testContext.components.settings.addressFeature } returns true
        every { testContext.components.settings.shouldAutofillCreditCardDetails } returns true
        every { testContext.components.settings.shouldAutofillAddressDetails } returns true
        every { testContext.components.settings.isAddressSyncEnabled } returns true

        autofillSettingFragment = AutofillSettingFragment()

        val activity = Robolectric.buildActivity(FragmentActivity::class.java).create().get()

        activity.supportFragmentManager
            .beginTransaction()
            .add(autofillSettingFragment, "CreditCardsSettingFragmentTest")
            .commitNow()
    }

    @Test
    fun `GIVEN the list of credit cards is not empty, WHEN fragment is displayed THEN the manage credit cards pref is 'Manage cards'`() =
        runTest {
            val preferenceTitle = testContext.getString(R.string.preferences_credit_cards_manage_saved_cards_2)
            val manageCardsPreference =
                autofillSettingFragment.findPreference<Preference>(
                    autofillSettingFragment.getPreferenceKey(R.string.pref_key_credit_cards_manage_cards)
                )

            val creditCards: List<CreditCard> = listOf(mockk(), mockk())

            val state = AutofillFragmentState(creditCards = creditCards)
            val store = AutofillFragmentStore(state)

            autofillSettingFragment.updateCardManagementPreference(
                store.state.creditCards.isNotEmpty(),
                navController,
            )

            assertNull(manageCardsPreference?.icon)
            assertEquals(preferenceTitle, manageCardsPreference?.title)
        }

    @Test
    fun `GIVEN the list of credit cards is empty, WHEN fragment is displayed THEN the manage credit cards pref is 'Add card'`() =
        runTest {
            val preferenceTitle = testContext.getString(R.string.preferences_credit_cards_add_credit_card_2)
            val manageCardsPreference =
                autofillSettingFragment.findPreference<Preference>(
                    autofillSettingFragment.getPreferenceKey(R.string.pref_key_credit_cards_manage_cards)
                )

            val directions = AutofillSettingFragmentDirections.actionAutofillSettingFragmentToCreditCardEditorFragment()

            val state = AutofillFragmentState()
            val store = AutofillFragmentStore(state)

            autofillSettingFragment.updateCardManagementPreference(
                store.state.creditCards.isNotEmpty(),
                navController,
            )

            assertNotNull(manageCardsPreference?.icon)
            assertEquals(preferenceTitle, manageCardsPreference.title)

            manageCardsPreference.performClick()

            verify { navController.navigate(directions) }
        }

    @Test
    fun `GIVEN the list of addresses is not empty WHEN fragment is displayed THEN the manage addresses preference label is 'Manage addresses'`() =
        runTest {
            val preferenceTitle = testContext.getString(R.string.preferences_addresses_manage_addresses)
            val manageAddressesPreference =
                autofillSettingFragment.findPreference<Preference>(
                    autofillSettingFragment.getPreferenceKey(R.string.pref_key_addresses_manage_addresses)
                )

            val addresses: List<Address> = listOf(mockk(), mockk())

            val state = AutofillFragmentState(addresses = addresses)
            val store = AutofillFragmentStore(state)

            autofillSettingFragment.updateAddressPreference(
                store.state.addresses.isNotEmpty(),
                navController,
            )

            assertNull(manageAddressesPreference?.icon)
            assertEquals(preferenceTitle, manageAddressesPreference?.title)

            manageAddressesPreference?.performClick()

            verify {
                navController.navigate(
                    AutofillSettingFragmentDirections.actionAutofillSettingFragmentToAddressManagementFragment()
                )
            }
        }

    @Test
    fun `GIVEN the list of addresses is empty WHEN fragment is displayed THEN the manage addresses preference label is 'Add address'`() =
        runTest {
            val preferenceTitle = testContext.getString(R.string.preferences_addresses_add_address)
            val manageAddressesPreference =
                autofillSettingFragment.findPreference<Preference>(
                    autofillSettingFragment.getPreferenceKey(R.string.pref_key_addresses_manage_addresses)
                )

            val state = AutofillFragmentState()
            val store = AutofillFragmentStore(state)

            autofillSettingFragment.updateAddressPreference(
                store.state.addresses.isNotEmpty(),
                navController,
            )

            assertNotNull(manageAddressesPreference?.icon)
            assertEquals(preferenceTitle, manageAddressesPreference.title)

            manageAddressesPreference.performClick()

            verify {
                navController.navigate(
                    AutofillSettingFragmentDirections.actionAutofillSettingFragmentToAddressEditorFragment()
                )
            }
        }

    @Test
    fun `GIVEN the autofill addresses feature is enabled THEN the addresses switch preference is checked`() = runTest {
        every { testContext.components.settings.shouldAutofillAddressDetails } returns true

        val autofillAddressesPreference =
            autofillSettingFragment.findPreference<SwitchPreferenceCompat>(
                autofillSettingFragment.getPreferenceKey(R.string.pref_key_addresses_save_and_autofill_addresses)
            )

        autofillSettingFragment.updateSaveAndAutofillAddressesSwitch()

        assertNotNull(autofillAddressesPreference)
        assertTrue(autofillAddressesPreference.isChecked)
    }

    @Test
    fun `GIVEN the autofill addresses feature & sync are enabled THEN the sync addresses preference is visible`() =
        runTest {
            every { testContext.components.settings.isAddressSyncEnabled } returns true

            autofillSettingFragment.updateAddressPreference(
                hasAddresses = false,
                navController = navController,
            )
            val addressSyncPreference =
                autofillSettingFragment.requirePreference<Preference>(
                    R.string.pref_key_addresses_sync_cards_across_devices
                )

            assertTrue(
                "Address sync preference should be visible when address sync is enabled",
                addressSyncPreference.isVisible,
            )
        }

    @Test
    fun `GIVEN the autofill addresses feature AND sync is not enabled THEN the sync addresses preference is hidden`() =
        runTest {
            every { testContext.components.settings.isAddressSyncEnabled } returns false

            autofillSettingFragment.updateAddressPreference(
                hasAddresses = false,
                navController = navController,
            )
            val addressSyncPreference =
                autofillSettingFragment.requirePreference<Preference>(
                    R.string.pref_key_addresses_sync_cards_across_devices
                )

            assertFalse(
                "Address sync preference should not be visible when address sync is disabled",
                addressSyncPreference.isVisible,
            )
        }

    @Test
    fun `GIVEN the autofill addresses feature is disabled THEN the addresses switch preference is NOT checked`() =
        runTest {
            every { testContext.components.settings.shouldAutofillAddressDetails } returns false

            val autofillAddressesPreference =
                autofillSettingFragment.findPreference<SwitchPreferenceCompat>(
                    autofillSettingFragment.getPreferenceKey(R.string.pref_key_addresses_save_and_autofill_addresses)
                )

            autofillSettingFragment.updateSaveAndAutofillAddressesSwitch()

            assertNotNull(autofillAddressesPreference)
            assertFalse(autofillAddressesPreference.isChecked)
        }

    @Test
    fun `GIVEN the autofill cards feature is enabled THEN cards the switch preference is checked`() = runTest {
        every { testContext.components.settings.shouldAutofillCreditCardDetails } returns true

        val autofillCardsPreference =
            autofillSettingFragment.findPreference<SwitchPreferenceCompat>(
                autofillSettingFragment.getPreferenceKey(R.string.pref_key_credit_cards_save_and_autofill_cards)
            )

        autofillSettingFragment.updateSaveAndAutofillCardsSwitch()

        assertNotNull(autofillCardsPreference)
        assertTrue(autofillCardsPreference.isChecked)
    }

    @Test
    fun `GIVEN the autofill cards feature is disabled THEN the cards switch preference is NOT checked`() = runTest {
        every { testContext.components.settings.shouldAutofillCreditCardDetails } returns false

        val autofillCardsPreference =
            autofillSettingFragment.findPreference<SwitchPreferenceCompat>(
                autofillSettingFragment.getPreferenceKey(R.string.pref_key_credit_cards_save_and_autofill_cards)
            )

        autofillSettingFragment.updateSaveAndAutofillCardsSwitch()

        assertNotNull(autofillCardsPreference)
        assertFalse(autofillCardsPreference.isChecked)
    }

    @Test
    fun `GIVEN the compose autofill settings WHEN managing credit cards THEN authentication is requested before navigating`() {
        every { testContext.components.settings.enableComposeAutofillSettings } returns true
        val composeFragment = AutofillSettingFragment()
        val activity = Robolectric.buildActivity(FragmentActivity::class.java).create().get()
        activity.supportFragmentManager
            .beginTransaction()
            .add(composeFragment, "ComposeAutofillSettingFragmentTest")
            .commitNow()
        val titles = mutableListOf<String>()
        composeFragment.automotiveAuthenticator =
            object : AutomotiveSensitiveDataAuthenticator {
                override fun isRequired(context: Context) = true

                override fun authenticate(
                    activity: FragmentActivity,
                    title: String,
                    onAuthenticated: () -> Unit,
                    onBlocked: () -> Unit,
                ) {
                    titles.add(title)
                }
            }

        composeFragment.goToFragment(AutofillScreenDestination.MANAGE_CREDIT_CARDS)

        assertEquals(listOf(testContext.getString(R.string.credit_cards_biometric_prompt_message)), titles)
    }
}
