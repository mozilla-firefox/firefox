/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.settings.autofill

import android.app.KeyguardManager
import android.content.Context
import android.content.DialogInterface
import android.content.Intent
import android.os.Bundle
import android.provider.Settings
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import androidx.annotation.VisibleForTesting
import androidx.compose.ui.platform.ComposeView
import androidx.compose.ui.platform.ViewCompositionStrategy
import androidx.lifecycle.lifecycleScope
import androidx.navigation.NavController
import androidx.navigation.NavHostController
import androidx.navigation.fragment.findNavController
import androidx.preference.Preference
import androidx.preference.SwitchPreferenceCompat
import com.google.android.material.R as materialR
import com.google.android.material.color.MaterialColors
import com.google.android.material.dialog.MaterialAlertDialogBuilder
import kotlin.Boolean
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import mozilla.components.concept.sync.SyncEngine
import mozilla.components.lib.state.ext.consumeFrom
import mozilla.components.lib.state.helpers.StoreProvider.Companion.fragmentStore
import mozilla.components.lib.state.helpers.StoreProvider.Companion.storeProvider
import mozilla.components.service.fxa.manager.SyncEnginesStorage
import mozilla.components.service.sync.autofill.AutofillCreditCardsAddressesStorage
import mozilla.components.ui.icons.R as iconsR
import mozilla.components.ui.widgets.withCenterAlignedButtons
import org.mozilla.fenix.Config
import org.mozilla.fenix.NavGraphDirections
import org.mozilla.fenix.R
import org.mozilla.fenix.components.LogMiddleware
import org.mozilla.fenix.components.accounts.FenixFxAEntryPoint
import org.mozilla.fenix.e2e.SystemInsetsPaddedFragment
import org.mozilla.fenix.ext.components
import org.mozilla.fenix.ext.hideToolbar
import org.mozilla.fenix.ext.requireComponents
import org.mozilla.fenix.ext.runIfFragmentIsAttached
import org.mozilla.fenix.ext.secure
import org.mozilla.fenix.ext.showToolbar
import org.mozilla.fenix.settings.SharedPreferenceUpdater
import org.mozilla.fenix.settings.SyncPreferenceView
import org.mozilla.fenix.settings.autofill.ui.AccountAuthState
import org.mozilla.fenix.settings.autofill.ui.AutofillSettingsMiddleware
import org.mozilla.fenix.settings.autofill.ui.AutofillSettingsScreen
import org.mozilla.fenix.settings.autofill.ui.AutofillSettingsState
import org.mozilla.fenix.settings.autofill.ui.AutofillSettingsStore
import org.mozilla.fenix.settings.biometric.BiometricPromptPreferenceFragment
import org.mozilla.fenix.settings.requirePreference
import org.mozilla.fenix.theme.FirefoxTheme

/**
 * Autofill settings fragment displays a list of settings related to auto filling, adding and syncing credit cards and
 * addresses.
 */
@SuppressWarnings("TooManyFunctions")
class AutofillSettingFragment : BiometricPromptPreferenceFragment(), SystemInsetsPaddedFragment {

    private lateinit var store: AutofillFragmentStore

    private var isAutofillStateLoaded: Boolean = false

    /** List of preferences to be enabled or disabled during authentication. */
    private val creditCardPreferences: List<Int> =
        listOf(
            R.string.pref_key_credit_cards_save_and_autofill_cards,
            R.string.pref_key_credit_cards_sync_cards_across_devices,
            R.string.pref_key_credit_cards_manage_cards,
        )

    override fun unlockMessage() = getString(R.string.credit_cards_biometric_prompt_message)

    override fun navigateOnSuccess() {
        runIfFragmentIsAttached {
            viewLifecycleOwner.lifecycleScope.launch(Dispatchers.Main) {
                // Workaround for likely biometric library bug
                // https://github.com/mozilla-mobile/fenix/issues/8438
                delay(SHORT_DELAY_MS)
                navigateToCreditCardManagementFragment()
            }
        }
    }

    override fun onCreateView(
        inflater: LayoutInflater,
        container: ViewGroup?,
        savedInstanceState: Bundle?,
    ): View {
        if (requireComponents.settings.enableComposeAutofillSettings) {
            return autofillSettingsComposeView()
        }

        store = storeProvider.get { restoredState ->
            AutofillFragmentStore(restoredState ?: AutofillFragmentState())
        }
        loadAutofillState()
        return super.onCreateView(inflater, container, savedInstanceState)
    }

    override fun onCreatePreferences(savedInstanceState: Bundle?, rootKey: String?) {
        if (requireComponents.settings.enableComposeAutofillSettings) {
            return
        }

        setPreferencesFromResource(
            if (requireComponents.settings.addressFeature) {
                R.xml.autofill_preferences
            } else {
                R.xml.credit_cards_preferences
            },
            rootKey,
        )

        updateSaveAndAutofillCardsSwitch()

        if (requireComponents.settings.addressFeature) {
            updateSaveAndAutofillAddressesSwitch()
        }
    }

    /** Updates save and autofill cards preference switch state depending on the saved user preference. */
    internal fun updateSaveAndAutofillCardsSwitch() {
        requirePreference<SwitchPreferenceCompat>(R.string.pref_key_credit_cards_save_and_autofill_cards).apply {
            isChecked = context.components.settings.shouldAutofillCreditCardDetails
            onPreferenceChangeListener = SharedPreferenceUpdater()
        }
    }

    /** Updates save and autofill addresses preference switch state depending on the saved user preference. */
    internal fun updateSaveAndAutofillAddressesSwitch() {
        requirePreference<SwitchPreferenceCompat>(R.string.pref_key_addresses_save_and_autofill_addresses).apply {
            isChecked = context.components.settings.shouldAutofillAddressDetails
            onPreferenceChangeListener = SharedPreferenceUpdater()
        }
    }

    private fun autofillSettingsComposeView(): View =
        ComposeView(requireContext()).apply {
            setViewCompositionStrategy(ViewCompositionStrategy.DisposeOnViewTreeLifecycleDestroyed)
            val buildStore = { _: NavHostController ->
                val syncEnginesStatus = SyncEnginesStorage(requireContext()).getStatus()
                val autofillStore by
                    fragmentStore(
                        AutofillSettingsState.default.copy(
                            saveFillAddresses = requireComponents.settings.shouldAutofillAddressDetails,
                            saveFillCards = requireComponents.settings.shouldAutofillCreditCardDetails,
                            syncAddresses = syncEnginesStatus.getOrElse(SyncEngine.Addresses) { false },
                            syncCreditCards = syncEnginesStatus.getOrElse(SyncEngine.CreditCards) { false },
                            accountAuthState =
                                if (requireComponents.settings.signedInFxaAccount) {
                                    AccountAuthState.Authenticated
                                } else {
                                    AccountAuthState.LoggedOut
                                },
                        )
                    ) {
                        AutofillSettingsStore(
                            initialState = it,
                            middleware =
                                listOf(
                                    LogMiddleware(
                                        tag = "AutofillSettingsStore",
                                        shouldIncludeDetailedData = { Config.channel.isDebug },
                                    ),
                                    createAutofillSettingsMiddleware(),
                                ),
                        )
                    }

                autofillStore
            }
            setContent {
                FirefoxTheme {
                    AutofillSettingsScreen(
                        buildStore = buildStore,
                        accountManager = requireComponents.backgroundServices.accountManager,
                        isAddressSyncEnabled = requireComponents.settings.isAddressSyncEnabled,
                    )
                }
            }
        }

    private fun createAutofillSettingsMiddleware() =
        AutofillSettingsMiddleware(
            autofillSettingsStorage = requireContext().components.core.autofillStorage,
            accountManager = requireComponents.backgroundServices.accountManager,
            updateSaveFillStatus = { destination, newValue ->
                updateSaveFillStatus(destination, newValue)
            },
            updateSyncStatusAcrossDevices = { destination, newValue ->
                updateSyncStatusAcrossDevices(destination, newValue)
            },
            goToScreen = { nextFragment ->
                goToFragment(nextFragment)
            },
            exitAutofillSettings = { this@AutofillSettingFragment.findNavController().popBackStack() },
        )

    override fun onViewCreated(view: View, savedInstanceState: Bundle?) {
        super.onViewCreated(view, savedInstanceState)

        if (requireComponents.settings.enableComposeAutofillSettings) {
            setBiometricPrompt(view, emptyList())
            return
        }

        requirePreference<SwitchPreferenceCompat>(R.string.pref_key_credit_cards_save_and_autofill_cards).summary =
            getString(R.string.preferences_credit_cards_save_and_autofill_cards_summary_2, getString(R.string.app_name))

        consumeFrom(store) { state ->
            if (requireComponents.settings.addressFeature) {
                updateAddressPreference(state.addresses.isNotEmpty(), findNavController())
            }
            updateCardManagementPreference(state.creditCards.isNotEmpty(), findNavController())
        }

        setBiometricPrompt(view, creditCardPreferences)
    }

    override fun onPause() {
        super.onPause()
        isAutofillStateLoaded = false
    }

    override fun onResume() {
        super.onResume()

        if (requireComponents.settings.enableComposeAutofillSettings) {
            hideToolbar()
            return
        }

        if (requireComponents.settings.addressFeature) {
            showToolbar(getString(R.string.preferences_autofill))
        } else {
            showToolbar(getString(R.string.preferences_credit_cards_2))
        }

        SyncPreferenceView(
            syncPreference = requirePreference(R.string.pref_key_credit_cards_sync_cards_across_devices),
            lifecycleOwner = viewLifecycleOwner,
            coroutineScope = viewLifecycleOwner.lifecycleScope,
            accountManager = requireComponents.backgroundServices.accountManager,
            syncEngine = SyncEngine.CreditCards,
            loggedOffTitle = requireContext().getString(R.string.preferences_credit_cards_sync_cards_across_devices),
            loggedInTitle = requireContext().getString(R.string.preferences_credit_cards_sync_cards),
            onSyncSignInClicked = {
                findNavController()
                    .navigate(
                        NavGraphDirections.actionGlobalTurnOnSync(entrypoint = FenixFxAEntryPoint.AutofillSetting)
                    )
            },
            onReconnectClicked = {
                findNavController()
                    .navigate(
                        AutofillSettingFragmentDirections.actionGlobalAccountProblemFragment(
                            entrypoint = FenixFxAEntryPoint.AutofillSetting
                        )
                    )
            },
        )

        if (requireComponents.settings.isAddressSyncEnabled) {
            SyncPreferenceView(
                syncPreference = requirePreference(R.string.pref_key_addresses_sync_cards_across_devices),
                lifecycleOwner = viewLifecycleOwner,
                coroutineScope = viewLifecycleOwner.lifecycleScope,
                accountManager = requireComponents.backgroundServices.accountManager,
                syncEngine = SyncEngine.Addresses,
                loggedOffTitle =
                    requireContext().getString(R.string.preferences_addresses_sync_addresses_across_devices),
                loggedInTitle = requireContext().getString(R.string.preferences_addresses_sync_addresses),
                onSyncSignInClicked = {
                    findNavController()
                        .navigate(
                            NavGraphDirections.actionGlobalTurnOnSync(entrypoint = FenixFxAEntryPoint.AutofillSetting)
                        )
                },
                onReconnectClicked = {
                    findNavController()
                        .navigate(
                            AutofillSettingFragmentDirections.actionGlobalAccountProblemFragment(
                                entrypoint = FenixFxAEntryPoint.AutofillSetting
                            )
                        )
                },
            )
        }

        togglePrefsEnabled(creditCardPreferences, true)
    }

    /** Updates preferences visibility depending on addresses being already saved or not. */
    @VisibleForTesting
    internal fun updateAddressPreference(
        hasAddresses: Boolean,
        navController: NavController,
    ) {
        val manageAddressesPreference = requirePreference<Preference>(R.string.pref_key_addresses_manage_addresses)

        // show address sync preference if address sync is enabled
        val addressSyncPreference = requirePreference<Preference>(R.string.pref_key_addresses_sync_cards_across_devices)
        addressSyncPreference.isVisible = requireComponents.settings.isAddressSyncEnabled

        if (hasAddresses) {
            manageAddressesPreference.icon = null
            manageAddressesPreference.title = getString(R.string.preferences_addresses_manage_addresses)
        } else {
            manageAddressesPreference.setIcon(iconsR.drawable.mozac_ic_plus_24)
            manageAddressesPreference.icon?.setTint(
                MaterialColors.getColor(
                    requireActivity(),
                    materialR.attr.colorOnSurface,
                    "Could not resolve themed color",
                )
            )
            manageAddressesPreference.title = getString(R.string.preferences_addresses_add_address)
        }

        manageAddressesPreference.setOnPreferenceClickListener {
            navController.navigate(
                if (hasAddresses) {
                    AutofillSettingFragmentDirections.actionAutofillSettingFragmentToAddressManagementFragment()
                } else {
                    AutofillSettingFragmentDirections.actionAutofillSettingFragmentToAddressEditorFragment()
                }
            )

            super.onPreferenceTreeClick(it)
        }
    }

    /** Updates preferences visibility depending on credit cards being already saved or not. */
    @VisibleForTesting
    internal fun updateCardManagementPreference(
        hasCreditCards: Boolean,
        navController: NavController,
    ) {
        val manageSavedCardsPreference = requirePreference<Preference>(R.string.pref_key_credit_cards_manage_cards)

        if (hasCreditCards) {
            manageSavedCardsPreference.icon = null
            manageSavedCardsPreference.title = getString(R.string.preferences_credit_cards_manage_saved_cards_2)
        } else {
            manageSavedCardsPreference.setIcon(iconsR.drawable.mozac_ic_plus_24)
            manageSavedCardsPreference.icon?.setTint(
                MaterialColors.getColor(
                    requireContext(),
                    materialR.attr.colorOnSurface,
                    "Could not resolve themed color",
                )
            )
            manageSavedCardsPreference.title = getString(R.string.preferences_credit_cards_add_credit_card_2)
        }

        manageSavedCardsPreference.setOnPreferenceClickListener {
            if (hasCreditCards) {
                verifyCredentialsOrShowSetupWarning(requireContext(), creditCardPreferences)
            } else {
                navController.navigate(
                    AutofillSettingFragmentDirections.actionAutofillSettingFragmentToCreditCardEditorFragment()
                )
            }
            super.onPreferenceTreeClick(it)
        }
    }

    /**
     * Fetches all the addresses and credit cards from [AutofillCreditCardsAddressesStorage] and updates the
     * [AutofillFragmentState].
     */
    private fun loadAutofillState() {
        if (isAutofillStateLoaded) {
            return
        }

        lifecycleScope.launch(Dispatchers.IO) {
            val addresses = requireComponents.core.autofillStorage.getAllAddresses()
            val creditCards = requireComponents.core.autofillStorage.getAllCreditCards()

            lifecycleScope.launch(Dispatchers.Main) {
                store.dispatch(AutofillAction.UpdateAddresses(addresses))
                store.dispatch(AutofillAction.UpdateCreditCards(creditCards))
            }
        }

        isAutofillStateLoaded = true
    }

    /**
     * Shows a dialog warning to set up a pin/password when the device is not secured. This is only used when
     * BiometricPrompt is unavailable on the device.
     */
    override fun showPinDialogWarning(context: Context) {
        MaterialAlertDialogBuilder(context)
            .apply {
                setTitle(getString(R.string.credit_cards_warning_dialog_title_2))
                setMessage(getString(R.string.credit_cards_warning_dialog_message_3))

                setNegativeButton(getString(R.string.credit_cards_warning_dialog_later)) { _: DialogInterface, _ ->
                    navigateToCreditCardManagementFragment()
                }

                setPositiveButton(getString(R.string.credit_cards_warning_dialog_set_up_now)) { it: DialogInterface, _
                    ->
                    it.dismiss()
                    val intent = Intent(Settings.ACTION_SECURITY_SETTINGS)
                    startActivity(intent)
                }

                create().withCenterAlignedButtons()
            }
            .show()
            .secure(activity)
        context.components.settings.incrementSecureWarningCount()
    }

    /**
     * Shows a prompt to verify the device's pin/password and start activity based on the result. This is only used when
     * BiometricPrompt is unavailable on the device.
     *
     * @param manager The device [KeyguardManager]
     */
    @Suppress("Deprecation")
    override fun showPinVerification(manager: KeyguardManager) {
        val intent =
            manager.createConfirmDeviceCredentialIntent(
                getString(R.string.credit_cards_biometric_prompt_message_pin),
                getString(R.string.credit_cards_biometric_prompt_message),
            )

        startForResult.launch(intent)
    }

    @VisibleForTesting
    internal fun goToFragment(destination: String) {
        with(AutofillScreenDestination) {
            when (destination) {
                ADD_ADDRESS -> {
                    navigateToAddAddressFragment()
                }
                MANAGE_ADDRESSES -> {
                    navigateToAddressManagementFragment()
                }
                ADD_CREDIT_CARD -> {
                    navigateToAddCreditCardFragment()
                }

                MANAGE_CREDIT_CARDS -> {
                    verifyCredentialsOrShowSetupWarning(requireContext(), emptyList())
                }
                SYNC_SIGN_IN -> {
                    syncSignIn()
                }
            }
        }
    }

    private fun navigateToAddAddressFragment() {
        val directions = AutofillSettingFragmentDirections.actionAutofillSettingFragmentToAddressEditorFragment()
        findNavController().navigate(directions)
    }

    private fun navigateToAddressManagementFragment() {
        val directions = AutofillSettingFragmentDirections.actionAutofillSettingFragmentToAddressManagementFragment()
        findNavController().navigate(directions)
    }

    private fun navigateToAddCreditCardFragment() {
        val directions = AutofillSettingFragmentDirections.actionAutofillSettingFragmentToCreditCardEditorFragment()
        findNavController().navigate(directions)
    }

    private fun navigateToCreditCardManagementFragment() {
        val directions =
            AutofillSettingFragmentDirections.actionAutofillSettingFragmentToCreditCardsManagementFragment()
        findNavController().navigate(directions)
    }

    private fun syncSignIn() {
        findNavController()
            .navigate(NavGraphDirections.actionGlobalTurnOnSync(entrypoint = FenixFxAEntryPoint.AutofillSetting))
    }

    private fun updateSyncStatusAcrossDevices(destination: String, newValue: Boolean) {
        val engine =
            when (destination) {
                AutofillScreenDestination.ADDRESS -> SyncEngine.Addresses
                AutofillScreenDestination.CREDIT_CARD -> SyncEngine.CreditCards
                else -> return
            }
        viewLifecycleOwner.lifecycleScope.launch {
            requireComponents.backgroundServices.accountManager.setEngineEnabled(engine, newValue)
        }
    }

    private fun updateSaveFillStatus(destination: String, newValue: Boolean) {
        when (destination) {
            AutofillScreenDestination.ADDRESS -> {
                requireComponents.settings.shouldAutofillAddressDetails = newValue
            }

            AutofillScreenDestination.CREDIT_CARD -> {
                requireComponents.settings.shouldAutofillCreditCardDetails = newValue
            }
        }
    }

    companion object {
        const val SHORT_DELAY_MS = 100L
    }
}
