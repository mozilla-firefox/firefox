/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.settings.account

import android.app.KeyguardManager
import android.content.Context
import android.content.DialogInterface
import android.content.Intent
import android.os.Bundle
import android.provider.Settings
import android.text.InputFilter
import android.text.format.DateUtils
import android.view.View
import androidx.core.content.edit
import androidx.lifecycle.lifecycleScope
import androidx.navigation.fragment.findNavController
import androidx.navigation.fragment.navArgs
import androidx.preference.CheckBoxPreference
import androidx.preference.EditTextPreference
import androidx.preference.Preference
import androidx.preference.PreferenceCategory
import androidx.preference.PreferenceFragmentCompat
import com.google.android.material.dialog.MaterialAlertDialogBuilder
import kotlinx.coroutines.Dispatchers.Main
import kotlinx.coroutines.launch
import mozilla.appservices.syncmanager.SyncTelemetry
import mozilla.components.concept.sync.AccountObserver
import mozilla.components.concept.sync.ConstellationState
import mozilla.components.concept.sync.DeviceConstellationObserver
import mozilla.components.lib.state.ext.consumeFrom
import mozilla.components.lib.state.helpers.StoreProvider.Companion.fragmentStore
import mozilla.components.service.fxa.SyncEngine
import mozilla.components.service.fxa.manager.FxaAccountManager
import mozilla.components.service.fxa.manager.SyncEnginesStorage
import mozilla.components.service.fxa.sync.SyncReason
import mozilla.components.service.fxa.sync.SyncStatusObserver
import mozilla.components.service.fxa.sync.getLastSynced
import mozilla.components.service.fxa.sync.setLastSynced
import mozilla.components.support.ktx.android.content.getColorFromAttr
import mozilla.components.ui.widgets.withCenterAlignedButtons
import mozilla.telemetry.glean.private.NoExtras
import org.mozilla.fenix.GleanMetrics.SyncAccount
import org.mozilla.fenix.R
import org.mozilla.fenix.components.accounts.FenixFxAEntryPoint
import org.mozilla.fenix.compose.snackbar.Snackbar
import org.mozilla.fenix.compose.snackbar.SnackbarState
import org.mozilla.fenix.e2e.SystemInsetsPaddedFragment
import org.mozilla.fenix.ext.components
import org.mozilla.fenix.ext.getPreferenceKey
import org.mozilla.fenix.ext.pixelSizeFor
import org.mozilla.fenix.ext.requireComponents
import org.mozilla.fenix.ext.secure
import org.mozilla.fenix.ext.settings
import org.mozilla.fenix.ext.showToolbar
import org.mozilla.fenix.settings.SupportUtils
import org.mozilla.fenix.settings.requirePreference
import org.mozilla.fenix.settings.scrollToPreferenceWithHighlight
import org.mozilla.fenix.settings.showCustomEditTextPreferenceDialog
import com.google.android.material.R as materialR

/**
 * Settings screen allowing users to manage their Firefox account and what data to sync through it.
 */
@SuppressWarnings("TooManyFunctions", "LargeClass")
class AccountSettingsFragment : PreferenceFragmentCompat(), SystemInsetsPaddedFragment {
    private lateinit var accountManager: FxaAccountManager
    private lateinit var accountSettingsStore: AccountSettingsFragmentStore
    private lateinit var accountSettingsInteractor: AccountSettingsInteractor
    private val args by navArgs<AccountSettingsFragmentArgs>()

    // Navigate away from this fragment when we encounter auth problems or logout events.
    private val accountStateObserver = object : AccountObserver {
        override fun onAuthenticationProblems() {
            viewLifecycleOwner.lifecycleScope.launch {
                findNavController().popBackStack()
            }
        }

        override fun onLoggedOut() {
            viewLifecycleOwner.lifecycleScope.launch {
                findNavController().popBackStack(R.id.accountSettingsFragment, inclusive = true)

                // Remove the device name when we log out.
                context?.let {
                    val deviceNameKey = it.getPreferenceKey(R.string.pref_key_sync_device_name)
                    preferenceManager.sharedPreferences?.edit { remove(deviceNameKey) }
                }
            }
        }
    }

    override fun onResume() {
        super.onResume()
        showToolbar(getString(R.string.preferences_account_settings))
        args.preferenceToScrollTo?.let {
            scrollToPreferenceWithHighlight(it)
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        SyncTelemetry.processOpenSyncSettingsMenuTelemetry()
        SyncAccount.opened.record(NoExtras())

        accountManager = requireComponents.backgroundServices.accountManager
        accountManager.register(accountStateObserver, this, true)
    }

    override fun onStop() {
        super.onStop()
        val allEngines = listOf(
            SyncEngine.Bookmarks,
            SyncEngine.Addresses,
            SyncEngine.CreditCards,
            SyncEngine.History,
            SyncEngine.Passwords,
            SyncEngine.Tabs,
        )
        val enabledEngines = mutableListOf<String>()
        val disabledEngines = mutableListOf<String>()
        val syncEnginesStatus = SyncEnginesStorage(requireContext()).getStatus()
        for (syncEngine in allEngines) {
            if (syncEnginesStatus.containsKey(syncEngine)) {
                if (syncEnginesStatus.getOrElse(syncEngine) { true }) {
                    enabledEngines.add(syncEngine.nativeName)
                } else {
                    disabledEngines.add(syncEngine.nativeName)
                }
            }
        }
        SyncTelemetry.processSaveSyncSettingsTelemetry(enabledEngines, disabledEngines)
    }

    override fun onViewCreated(view: View, savedInstanceState: Bundle?) {
        super.onViewCreated(view, savedInstanceState)

        accountSettingsStore = fragmentStore(
            AccountSettingsFragmentState(
                lastSyncedDate = if (getLastSynced(requireContext()) == 0L) {
                    LastSyncTime.Never
                } else {
                    LastSyncTime.Success(getLastSynced(requireContext()))
                },
                deviceName = requireComponents.backgroundServices.defaultDeviceName(
                    requireContext(),
                ),
            ),
        ) { AccountSettingsFragmentStore(it) }.value

        consumeFrom(accountSettingsStore) {
            updateLastSyncTimePref(it)
            updateDeviceName(it)
        }

        accountSettingsInteractor = AccountSettingsInteractor(
            findNavController(),
            ::syncNow,
            ::syncDeviceName,
            accountSettingsStore,
        )

        setupPreferenceListeners()
    }

    @Suppress("LongMethod", "CyclomaticComplexMethod")
    override fun onCreatePreferences(savedInstanceState: Bundle?, rootKey: String?) {
        setPreferencesFromResource(R.xml.account_settings_preferences, rootKey)
    }

    override fun onDisplayPreferenceDialog(preference: Preference) {
        val handled = showCustomEditTextPreferenceDialog(
            preference = preference,
            errorMessage = { value -> R.string.empty_device_name_error.takeIf { value.isBlank() } },
        )

        if (!handled) {
            super.onDisplayPreferenceDialog(preference)
        }
    }

    private fun setupPreferenceListeners() {
        val preferenceManageAccount = requirePreference<Preference>(R.string.pref_key_sync_manage_account)
        preferenceManageAccount.onPreferenceClickListener = getClickListenerForManageAccount()

        setupSignInOutPreferenceListeners()
        setupDeviceNamePreferenceListeners()
        setupSyncCategoriesPreferenceListeners()

        // NB: ObserverRegistry will take care of cleaning up internal references to 'observer' and
        // 'owner' when appropriate.
        requireComponents.backgroundServices.accountManager.registerForSyncEvents(
            syncStatusObserver,
            owner = this,
            autoPause = true,
        )
    }

    private fun setupSignInOutPreferenceListeners() {
        // Sign out
        val preferenceSignOut = requirePreference<Preference>(R.string.pref_key_sign_out)
        preferenceSignOut.onPreferenceClickListener = getClickListenerForSignOut()

        // Sync now
        val preferenceSyncNow = requirePreference<Preference>(R.string.pref_key_sync_now)
        preferenceSyncNow.apply {
            onPreferenceClickListener = getClickListenerForSyncNow()

            icon?.let {
                icon = it.mutate().apply {
                    setTint(context.getColorFromAttr(materialR.attr.colorOnSurface))
                }
            }

            // Current sync state
            if (requireComponents.backgroundServices.accountManager.isSyncActive()) {
                title = getString(R.string.sync_syncing_in_progress)
                isEnabled = false
            } else {
                isEnabled = true
            }
        }
    }

    private fun setupDeviceNamePreferenceListeners() {
        val deviceConstellation = accountManager.authenticatedAccount()?.deviceConstellation()

        requirePreference<EditTextPreference>(R.string.pref_key_sync_device_name).apply {
            onPreferenceChangeListener = getChangeListenerForDeviceName()
            deviceConstellation?.state()?.currentDevice?.let { device ->
                summary = device.displayName
                text = device.displayName
                accountSettingsStore.dispatch(AccountSettingsFragmentAction.UpdateDeviceName(device.displayName))
            }
            setOnBindEditTextListener { editText ->
                editText.filters = arrayOf(InputFilter.LengthFilter(DEVICE_NAME_MAX_LENGTH))
                editText.minHeight = pixelSizeFor(R.dimen.account_settings_device_name_min_height)
            }
        }

        deviceConstellation?.registerDeviceObserver(
            deviceConstellationObserver,
            owner = this,
            autoPause = true,
        )
    }

    private fun setupSyncCategoriesPreferenceListeners() {
        // Make sure out sync engine checkboxes are up-to-date and disabled if currently syncing
        updateSyncEngineStates()
        setDisabledWhileSyncing(accountManager.isSyncActive())

        fun SyncEngine.prefId(): Int = when (this) {
            SyncEngine.History -> R.string.pref_key_sync_history
            SyncEngine.Bookmarks -> R.string.pref_key_sync_bookmarks
            SyncEngine.Passwords -> R.string.pref_key_sync_logins
            SyncEngine.Tabs -> R.string.pref_key_sync_tabs
            SyncEngine.CreditCards -> R.string.pref_key_sync_credit_cards
            SyncEngine.Addresses -> R.string.pref_key_sync_address
            else -> throw IllegalStateException("Accessing internal sync engines")
        }

        listOf(
            SyncEngine.History,
            SyncEngine.Bookmarks,
            SyncEngine.Tabs,
            SyncEngine.Addresses,
        ).forEach {
            requirePreference<CheckBoxPreference>(it.prefId()).apply {
                setOnPreferenceChangeListener { _, newValue ->
                    updateSyncEngineState(it, newValue as Boolean)
                    true
                }
            }
        }

        // 'Passwords' and 'Credit card' listeners are special, since we also display a pin protection warning.
        listOf(
            SyncEngine.Passwords,
            SyncEngine.CreditCards,
        ).forEach {
            requirePreference<CheckBoxPreference>(it.prefId()).apply {
                setOnPreferenceChangeListener { _, newValue ->
                    updateSyncEngineStateWithPinWarning(it, newValue as Boolean)
                    true
                }
            }
        }
    }

    /**
     * Prompts the user if they do not have a password/pin set up to secure their device, and
     * updates the state of the sync engine with the new checkbox value.
     *
     * Currently used for logins and credit cards.
     *
     * @param syncEngine the sync engine whose preference has changed.
     * @param newValue the value denoting whether or not to sync the specified preference.
     */
    private fun updateSyncEngineStateWithPinWarning(
        syncEngine: SyncEngine,
        newValue: Boolean,
    ) {
        val manager = activity?.getSystemService(Context.KEYGUARD_SERVICE) as KeyguardManager

        if (manager.isKeyguardSecure ||
            !newValue ||
            !requireContext().settings().shouldShowSecurityPinWarningSync
        ) {
            updateSyncEngineState(syncEngine, newValue)
        } else {
            showPinDialogWarning(syncEngine, newValue)
        }
    }

    /**
     * Updates the sync engine status with the new state of the preference and triggers a sync
     * event.
     *
     * @param engine the sync engine whose preference has changed.
     * @param newValue the new value of the sync preference, where true indicates sync for that
     * preference and false indicates not synced.
     */
    private fun updateSyncEngineState(engine: SyncEngine, newValue: Boolean) {
        SyncEnginesStorage(requireContext()).setStatus(engine, newValue)
        viewLifecycleOwner.lifecycleScope.launch {
            requireContext().components.backgroundServices.accountManager.syncNow(SyncReason.EngineChange)
        }
    }

    /**
     * Creates and shows a warning dialog that prompts the user to create a pin/password to
     * secure their device when none is detected. The user has the option to continue with
     * updating their sync preferences (updates the [SyncEngine] state) or navigating to
     * device security settings to create a pin/password.
     *
     * @param syncEngine the sync engine whose preference has changed.
     * @param newValue the new value of the sync preference, where true indicates sync for that
     * preference and false indicates not synced.
     */
    private fun showPinDialogWarning(syncEngine: SyncEngine, newValue: Boolean) {
        context?.let {
            MaterialAlertDialogBuilder(it).apply {
                setTitle(getString(R.string.logins_warning_dialog_title_2))
                setMessage(
                    getString(R.string.logins_warning_dialog_message_2),
                )

                setNegativeButton(getString(R.string.logins_warning_dialog_later)) { _: DialogInterface, _ ->
                    updateSyncEngineState(syncEngine, newValue)
                }

                setPositiveButton(getString(R.string.logins_warning_dialog_set_up_now)) { it: DialogInterface, _ ->
                    it.dismiss()
                    val intent = Intent(
                        Settings.ACTION_SECURITY_SETTINGS,
                    )
                    startActivity(intent)
                }
                create().withCenterAlignedButtons()
            }.show().secure(activity)
            it.settings().incrementShowLoginsSecureWarningSyncCount()
        }
    }

    /**
     * Updates the status of all [SyncEngine] states.
     */
    private fun updateSyncEngineStates() {
        val settings = requireContext().settings()
        val syncEnginesStatus = SyncEnginesStorage(requireContext()).getStatus()
        requirePreference<CheckBoxPreference>(R.string.pref_key_sync_bookmarks).apply {
            isEnabled = syncEnginesStatus.containsKey(SyncEngine.Bookmarks)
            isChecked = syncEnginesStatus.getOrElse(SyncEngine.Bookmarks) { true }
        }
        requirePreference<CheckBoxPreference>(R.string.pref_key_sync_credit_cards).apply {
            isEnabled = syncEnginesStatus.containsKey(SyncEngine.CreditCards)
            isChecked = syncEnginesStatus.getOrElse(SyncEngine.CreditCards) { true }
        }
        requirePreference<CheckBoxPreference>(R.string.pref_key_sync_history).apply {
            isEnabled = syncEnginesStatus.containsKey(SyncEngine.History)
            isChecked = syncEnginesStatus.getOrElse(SyncEngine.History) { true }
        }
        requirePreference<CheckBoxPreference>(R.string.pref_key_sync_logins).apply {
            isEnabled = syncEnginesStatus.containsKey(SyncEngine.Passwords)
            isChecked = syncEnginesStatus.getOrElse(SyncEngine.Passwords) { true }
        }
        requirePreference<CheckBoxPreference>(R.string.pref_key_sync_tabs).apply {
            isEnabled = syncEnginesStatus.containsKey(SyncEngine.Tabs)
            isChecked = syncEnginesStatus.getOrElse(SyncEngine.Tabs) { true }
        }
        requirePreference<CheckBoxPreference>(R.string.pref_key_sync_address).apply {
            isVisible = settings.isAddressSyncEnabled
            isEnabled = syncEnginesStatus.containsKey(SyncEngine.Addresses)
            isChecked = syncEnginesStatus.getOrElse(SyncEngine.Addresses) { true }
        }
    }

    /**
     * Manual sync triggered by the user. This also checks account authentication and refreshes the
     * device list.
     */
    private fun syncNow() {
        viewLifecycleOwner.lifecycleScope.launch {
            SyncAccount.syncNow.record(NoExtras())
            // Trigger a sync.
            requireComponents.backgroundServices.accountManager.syncNow(SyncReason.User)
            // Poll for device events & update devices.
            accountManager.authenticatedAccount()
                ?.deviceConstellation()?.run {
                    refreshDevices()
                    pollForCommands()
                }
        }
    }

    /**
     * Takes a non-empty value and sets the device name. May fail due to authentication.
     *
     * @param newDeviceName the new name of the device. Cannot be an empty string.
     */
    private fun syncDeviceName(newDeviceName: String): Boolean {
        if (newDeviceName.trim().isEmpty()) {
            return false
        }
        // This may fail, and we'll have a disparity in the UI until `updateDeviceName` is called.
        viewLifecycleOwner.lifecycleScope.launch(Main) {
            context?.let {
                accountManager.authenticatedAccount()
                    ?.deviceConstellation()
                    ?.setDeviceName(newDeviceName, it)
            }
            accountSettingsStore.dispatch(AccountSettingsFragmentAction.UpdateDeviceName(newDeviceName))
        }
        return true
    }

    private fun getClickListenerForManageAccount(): Preference.OnPreferenceClickListener {
        return Preference.OnPreferenceClickListener {
            viewLifecycleOwner.lifecycleScope.launch(Main) {
                context?.let {
                    var acct = accountManager.authenticatedAccount()
                    var url = acct?.getManageAccountURL(FenixFxAEntryPoint.SettingsMenu)
                    if (url != null) {
                        val intent = SupportUtils.createCustomTabIntent(it, url)
                        startActivity(intent)
                    }
                }
            }
            SyncAccount.manageAccount.record(NoExtras())
            true
        }
    }

    private fun getClickListenerForSignOut(): Preference.OnPreferenceClickListener {
        return Preference.OnPreferenceClickListener {
            accountSettingsInteractor.onSignOut()
            true
        }
    }

    private fun getClickListenerForSyncNow(): Preference.OnPreferenceClickListener {
        return Preference.OnPreferenceClickListener {
            accountSettingsInteractor.onSyncNow()
            true
        }
    }

    private fun getChangeListenerForDeviceName(): Preference.OnPreferenceChangeListener {
        return Preference.OnPreferenceChangeListener { _, newValue ->
            accountSettingsInteractor.onChangeDeviceName(newValue as String) {
                Snackbar.make(
                    snackBarParentView = requireView(),
                    snackbarState = SnackbarState(
                        message = getString(R.string.empty_device_name_error),
                        duration = SnackbarState.Duration.Preset.Long,
                    ),
                ).show()
            }
        }
    }

    private fun setDisabledWhileSyncing(isSyncing: Boolean) {
        requirePreference<PreferenceCategory>(R.string.preferences_sync_category).isEnabled = !isSyncing
        requirePreference<EditTextPreference>(R.string.pref_key_sync_device_name).isEnabled = !isSyncing
    }

    private val syncStatusObserver = object : SyncStatusObserver {
        private val pref by lazy { requirePreference<Preference>(R.string.pref_key_sync_now) }

        override fun onStarted() {
            viewLifecycleOwner.lifecycleScope.launch {
                @Suppress("DEPRECATION")
                view?.announceForAccessibility(getString(R.string.sync_syncing_in_progress))
                pref.title = getString(R.string.sync_syncing_in_progress)
                pref.isEnabled = false
                setDisabledWhileSyncing(true)
            }
        }

        // Sync stopped successfully.
        override fun onIdle() {
            viewLifecycleOwner.lifecycleScope.launch {
                pref.title = getString(R.string.preferences_sync_now)
                pref.isEnabled = true

                accountSettingsStore.dispatch(
                    AccountSettingsFragmentAction.SyncEnded(
                        lastSavedSyncTime(),
                    ),
                )
                // Make sure out sync engine checkboxes are up-to-date.
                updateSyncEngineStates()
                setDisabledWhileSyncing(false)
            }
        }

        // Sync stopped after encountering a problem.
        override fun onError(error: Exception?) {
            viewLifecycleOwner.lifecycleScope.launch {
                pref.title = getString(R.string.preferences_sync_now)
                // We want to only enable the sync button, and not the checkboxes here
                pref.isEnabled = true

                accountSettingsStore.dispatch(
                    AccountSettingsFragmentAction.SyncFailed(
                        lastSavedSyncTime(),
                    ),
                )
            }
        }

        // Returns the last saved sync time (in millis)
        // If the corresponding shared preference doesn't have a value yet,
        // it is initialized with the current time (in millis)
        private fun lastSavedSyncTime(): Long {
            val lastSyncedTime = getLastSynced(requireContext())
            return if (lastSyncedTime != 0L) {
                lastSyncedTime
            } else {
                val current = System.currentTimeMillis()
                setLastSynced(requireContext(), current)
                current
            }
        }
    }

    private val deviceConstellationObserver = object : DeviceConstellationObserver {
        override fun onDevicesUpdate(constellation: ConstellationState) {
            constellation.currentDevice?.displayName?.also {
                accountSettingsStore.dispatch(AccountSettingsFragmentAction.UpdateDeviceName(it))
            }
        }
    }

    private fun updateDeviceName(state: AccountSettingsFragmentState) {
        val preferenceDeviceName = requirePreference<Preference>(R.string.pref_key_sync_device_name)
        preferenceDeviceName.summary = state.deviceName
    }

    private fun updateLastSyncTimePref(state: AccountSettingsFragmentState) {
        val value = when (state.lastSyncedDate) {
            LastSyncTime.Never -> getString(R.string.sync_never_synced_summary)
            is LastSyncTime.Failed -> {
                if (state.lastSyncedDate.lastSync == 0L) {
                    getString(R.string.sync_failed_never_synced_summary)
                } else {
                    getString(
                        R.string.sync_failed_summary,
                        DateUtils.getRelativeTimeSpanString(state.lastSyncedDate.lastSync),
                    )
                }
            }
            is LastSyncTime.Success -> String.format(
                getString(R.string.sync_last_synced_summary),
                DateUtils.getRelativeTimeSpanString(state.lastSyncedDate.lastSync),
            )
        }

        requirePreference<Preference>(R.string.pref_key_sync_now).summary = value
    }

    companion object {
        private const val DEVICE_NAME_MAX_LENGTH = 128
    }
}
