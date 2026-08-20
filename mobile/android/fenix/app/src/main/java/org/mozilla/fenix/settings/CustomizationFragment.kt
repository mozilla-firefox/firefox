/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.settings

import android.content.Context
import android.content.res.Configuration
import android.os.Build
import android.os.Build.VERSION.SDK_INT
import android.os.Bundle
import android.view.View
import androidx.appcompat.app.AppCompatDelegate
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.lifecycleScope
import androidx.lifecycle.repeatOnLifecycle
import androidx.navigation.fragment.navArgs
import androidx.preference.Preference
import androidx.preference.PreferenceCategory
import androidx.preference.PreferenceFragmentCompat
import androidx.preference.SwitchPreferenceCompat
import kotlinx.coroutines.flow.combine
import kotlinx.coroutines.flow.distinctUntilChanged
import kotlinx.coroutines.launch
import mozilla.components.feature.summarize.settings.SummarizationSettings
import org.mozilla.fenix.FeatureFlags
import org.mozilla.fenix.GleanMetrics.AppTheme
import org.mozilla.fenix.GleanMetrics.CustomizationSettings
import org.mozilla.fenix.GleanMetrics.PullToRefreshInBrowser
import org.mozilla.fenix.GleanMetrics.ToolbarSettings
import org.mozilla.fenix.R
import org.mozilla.fenix.components.toolbar.ToolbarPosition
import org.mozilla.fenix.e2e.SystemInsetsPaddedFragment
import org.mozilla.fenix.ext.components
import org.mozilla.fenix.ext.isTallWindow
import org.mozilla.fenix.ext.isWideWindow
import org.mozilla.fenix.ext.requireComponents
import org.mozilla.fenix.ext.showToolbar
import org.mozilla.fenix.settings.settingssearch.PreferenceFileInformation.CustomizationPreferences
import org.mozilla.fenix.settings.settingssearch.SettingsSearchItem
import org.mozilla.fenix.settings.settingssearch.SettingsSearchProvider
import org.mozilla.fenix.translations.TranslationsEnabledSettings
import org.mozilla.fenix.utils.Settings
import org.mozilla.fenix.utils.view.addToRadioGroup

/** Lets the user customize the UI. */
@Suppress("TooManyFunctions")
class CustomizationFragment : PreferenceFragmentCompat(), SystemInsetsPaddedFragment {
    private lateinit var radioLightTheme: RadioButtonPreference
    private lateinit var radioDarkTheme: RadioButtonPreference
    private lateinit var radioDarkestTheme: RadioButtonPreference
    private lateinit var radioAutoBatteryTheme: RadioButtonPreference
    private lateinit var radioFollowDeviceTheme: RadioButtonPreference
    private val args by navArgs<CustomizationFragmentArgs>()

    // Cached reactive feature state used when (re)building preferences.
    // Defaults match the DataStore defaults (summarization off, translations on)
    private var dynamicFeaturesStatus =
        DynamicFeaturesStatus(
            isSummarizationFeatureEnabled = false,
            isSummarizationGestureEnabled = false,
            isTranslationsFeatureEnabled = true,
            isTranslationsFeatureSupported = false,
        )

    override fun onCreatePreferences(savedInstanceState: Bundle?, rootKey: String?) {
        setPreferencesFromResource(R.xml.customization_preferences, rootKey)

        setupPreferences(dynamicFeaturesStatus)
    }

    override fun onViewCreated(view: View, savedInstanceState: Bundle?) {
        super.onViewCreated(view, savedInstanceState)

        viewLifecycleOwner.lifecycleScope.launch {
            viewLifecycleOwner.repeatOnLifecycle(Lifecycle.State.CREATED) {
                val summarizationDataStore = SummarizationSettings.dataStore(requireContext())
                val translationsDataStore = TranslationsEnabledSettings.dataStore(requireContext())
                val browserStore = requireComponents.core.store
                combine(
                        summarizationDataStore.getFeatureEnabledUserStatus(),
                        summarizationDataStore.getGestureEnabledUserStatus(),
                        translationsDataStore.isEnabled,
                        browserStore.stateFlow,
                    ) { isSummarizationEnabled, isSummarizationGestureEnabled, areTranslationsEnabled, browserState ->
                        DynamicFeaturesStatus(
                            isSummarizationFeatureEnabled = isSummarizationEnabled == true,
                            isSummarizationGestureEnabled = isSummarizationGestureEnabled,
                            isTranslationsFeatureEnabled = areTranslationsEnabled,
                            isTranslationsFeatureSupported = browserState.translationEngine.isEngineSupported ?: false,
                        )
                    }
                    .distinctUntilChanged()
                    .collect { status ->
                        setupPreferences(status)
                    }
            }
        }
    }

    override fun onResume() {
        super.onResume()
        showToolbar(getString(R.string.preferences_customize))
        args.preferenceToScrollTo?.let {
            scrollToPreferenceWithHighlight(it)
        }
    }

    private fun setupPreferences(status: DynamicFeaturesStatus) {
        dynamicFeaturesStatus = status

        bindFollowDeviceTheme()
        bindDarkTheme()
        bindDarkestTheme()
        bindLightTheme()
        bindAutoBatteryTheme()
        setupRadioGroups()
        val tabletAndTabStripEnabled = Settings(requireContext()).isTabStripEnabled
        updateToolbarCategoryBasedOnTabStrip(tabletAndTabStripEnabled)
        setupTabStripCategory()
        setupToolbarLayout()
        updateToolbarShortcut()

        // if tab strip is enabled, swipe toolbar to switch tabs should not be enabled so the
        // preference is not shown
        setupGesturesCategory(
            isSwipeToolbarToSwitchTabsVisible = !tabletAndTabStripEnabled,
            isSummarizationEnabled = status.isSummarizationFeatureEnabled,
            isSummarizationGestureEnabled = status.isSummarizationGestureEnabled,
        )
    }

    private fun updateToolbarCategoryBasedOnTabStrip(tabStripEnabled: Boolean) {
        val topPreference = requirePreference<RadioButtonPreference>(R.string.pref_key_toolbar_top)
        val bottomPreference = requirePreference<RadioButtonPreference>(R.string.pref_key_toolbar_bottom)
        val tabStripMessagePref = findPreference<Preference>(getString(R.string.pref_key_tab_strip_message))

        topPreference.isEnabled = !tabStripEnabled
        bottomPreference.isEnabled = !tabStripEnabled
        tabStripMessagePref?.isVisible = tabStripEnabled

        if (tabStripEnabled && !topPreference.isChecked) {
            topPreference.setCheckedWithoutClickListener(true)
            bottomPreference.setCheckedWithoutClickListener(false)
        } else {
            setupToolbarCategory()
        }
    }

    private fun updateToolbarShortcut() {
        val category = requirePreference<PreferenceCategory>(R.string.pref_key_customization_category_toolbar_shortcut)
        val settings = requireComponents.settings
        val isExpandedToolbarEnabled = settings.shouldUseExpandedToolbar && isTallWindow() && !isWideWindow()
        val isAnyShortcutSelectedForSimpleToolbar = settings.toolbarSimpleShortcutKey != ShortcutType.NONE.value

        val shortcutPreference =
            if (isExpandedToolbarEnabled) {
                buildExpandedToolbarCustomButtonSetting()
            } else if (settings.isTabStripEnabled) {
                if (settings.toolbarTabStripShortcutKey != ShortcutType.NONE.value) {
                    buildTabStripToolbarWithCustomButtonSelectedSetting()
                } else {
                    buildTabStripToolbarWithNoCustomButtonSelectedSetting()
                }
            } else if (isAnyShortcutSelectedForSimpleToolbar) {
                buildSimpleToolbarWithCustomButtonSelectedSetting()
            } else {
                buildSimpleToolbarWithNoCustomButtonSelectedSetting()
            }
        category.apply {
            removeAll()
            addPreference(shortcutPreference)
            val shortcutOptions = shortcutPreference.getShortcutOptions()
            shortcutOptions.forEach(::addPreference)
        }
    }

    private fun buildExpandedToolbarCustomButtonSetting() =
        ToolbarExpandedShortcutPreference(requireContext()).apply {
            key = getString(R.string.pref_key_toolbar_expanded_shortcut)
            layoutResource = R.layout.preference_toolbar_shortcut
            isTranslationsFeatureEnabled = dynamicFeaturesStatus.isTranslationsFeatureEnabled
        }

    private fun buildSimpleToolbarWithCustomButtonSelectedSetting() =
        ToolbarSimpleShortcutPreference(requireContext()).apply {
            key = getString(R.string.pref_key_toolbar_simple_shortcut)
            layoutResource = R.layout.preference_toolbar_shortcut
            isTranslationsFeatureEnabled = dynamicFeaturesStatus.isTranslationsFeatureEnabled
            optionChangedListener = { newOption ->
                if (newOption == null || newOption.key.value == ShortcutType.NONE.value) {
                    updateToolbarShortcut()
                }
            }
        }

    private fun buildSimpleToolbarWithNoCustomButtonSelectedSetting() =
        ToolbarSimpleNoShortcutPreference(requireContext()).apply {
            key = getString(R.string.pref_key_toolbar_simple_no_shortcut)
            layoutResource = R.layout.preference_toolbar_shortcut
            isTranslationsFeatureEnabled = dynamicFeaturesStatus.isTranslationsFeatureEnabled
            optionChangedListener = { newOption ->
                if (newOption == null || newOption.key.value != ShortcutType.NONE.value) {
                    updateToolbarShortcut()
                }
            }
        }

    private fun buildTabStripToolbarWithCustomButtonSelectedSetting() =
        ToolbarTabStripShortcutPreference(requireContext()).apply {
            key = getString(R.string.pref_key_toolbar_tab_strip_shortcut)
            layoutResource = R.layout.preference_toolbar_shortcut
            isTranslationsFeatureEnabled = dynamicFeaturesStatus.isTranslationsFeatureEnabled
            optionChangedListener = { newOption ->
                if (newOption == null || newOption.key.value == ShortcutType.NONE.value) {
                    updateToolbarShortcut()
                }
            }
        }

    private fun buildTabStripToolbarWithNoCustomButtonSelectedSetting() =
        ToolbarTabStripNoShortcutPreference(requireContext()).apply {
            key = getString(R.string.pref_key_toolbar_tab_strip_no_shortcut)
            layoutResource = R.layout.preference_toolbar_shortcut
            isTranslationsFeatureEnabled = dynamicFeaturesStatus.isTranslationsFeatureEnabled
            optionChangedListener = { newOption ->
                if (newOption == null || newOption.key.value != ShortcutType.NONE.value) {
                    updateToolbarShortcut()
                }
            }
        }

    private fun setupRadioGroups() {
        addToRadioGroup(
            radioLightTheme,
            radioDarkTheme,
            radioDarkestTheme,
            if (SDK_INT >= Build.VERSION_CODES.P) {
                radioFollowDeviceTheme
            } else {
                radioAutoBatteryTheme
            },
        )
    }

    private fun bindLightTheme() {
        radioLightTheme = requirePreference(R.string.pref_key_light_theme)
        radioLightTheme.onClickListener {
            setNewTheme(AppCompatDelegate.MODE_NIGHT_NO)
        }
    }

    private fun bindAutoBatteryTheme() {
        radioAutoBatteryTheme = requirePreference(R.string.pref_key_auto_battery_theme)
        radioAutoBatteryTheme.onClickListener {
            setNewTheme(AppCompatDelegate.MODE_NIGHT_AUTO_BATTERY)
        }
    }

    private fun bindDarkTheme() {
        radioDarkTheme = requirePreference(R.string.pref_key_dark_theme)
        radioDarkTheme.onClickListener {
            AppTheme.darkThemeSelected.record(AppTheme.DarkThemeSelectedExtra("SETTINGS"))
            setNewTheme(AppCompatDelegate.MODE_NIGHT_YES)
        }
    }

    private fun bindDarkestTheme() {
        val settings = requireComponents.settings
        radioDarkestTheme = requirePreference(R.string.pref_key_oled_theme)
        radioDarkestTheme.isVisible = settings.enableOledTheme
    }

    private fun bindFollowDeviceTheme() {
        radioFollowDeviceTheme = requirePreference(R.string.pref_key_follow_device_theme)
        if (SDK_INT >= Build.VERSION_CODES.P) {
            radioFollowDeviceTheme.onClickListener {
                setNewTheme(AppCompatDelegate.MODE_NIGHT_FOLLOW_SYSTEM)
            }
        }
    }

    private fun setNewTheme(mode: Int) {
        if (AppCompatDelegate.getDefaultNightMode() == mode) return
        AppCompatDelegate.setDefaultNightMode(mode)
        activity?.recreate()
        with(requireComponents.core) {
            engine.settings.preferredColorScheme = getPreferredColorScheme()
        }
        requireComponents.useCases.sessionUseCases.reload.invoke()
    }

    private fun setupToolbarCategory() {
        val topPreference = requirePreference<RadioButtonPreference>(R.string.pref_key_toolbar_top)
        topPreference.onClickListener {
            ToolbarSettings.changedPosition.record(ToolbarSettings.ChangedPositionExtra(Position.TOP.name))

            updateToolbarLayoutIcons()
        }

        val bottomPreference = requirePreference<RadioButtonPreference>(R.string.pref_key_toolbar_bottom)
        bottomPreference.onClickListener {
            ToolbarSettings.changedPosition.record(ToolbarSettings.ChangedPositionExtra(Position.BOTTOM.name))

            updateToolbarLayoutIcons()
        }

        val toolbarPosition = requireComponents.settings.toolbarPosition
        topPreference.setCheckedWithoutClickListener(toolbarPosition == ToolbarPosition.TOP)
        bottomPreference.setCheckedWithoutClickListener(toolbarPosition == ToolbarPosition.BOTTOM)

        addToRadioGroup(topPreference, bottomPreference)
    }

    private fun setupTabStripCategory() {
        val tabStripSwitch = requirePreference<SwitchPreferenceCompat>(R.string.pref_key_tab_strip_show)
        val context = requireContext()

        tabStripSwitch.isChecked = Settings(requireContext()).isTabStripEnabled

        tabStripSwitch.setOnPreferenceChangeListener { _, newValue ->
            val enabled = newValue as Boolean
            context.components.settings.isTabStripEnabled = enabled
            updateToolbarCategoryBasedOnTabStrip(enabled)
            setupToolbarLayout()
            updateToolbarShortcut()
            true
        }
    }

    private fun setupToolbarLayout() {
        (requirePreference(R.string.pref_key_customization_category_toolbar_layout) as PreferenceCategory).apply {
            isVisible = isTallWindow() && !isWideWindow()
        }

        val layoutToggle = requirePreference<ToggleRadioButtonPreference>(R.string.pref_key_toolbar_expanded)
        layoutToggle.setOnToggleChanged {
            updateToolbarShortcut()
        }
        updateToolbarLayoutIcons()
    }

    private fun updateToolbarLayoutIcons() {
        (requirePreference(R.string.pref_key_toolbar_expanded) as ToggleRadioButtonPreference).apply {
            if (requireComponents.settings.shouldUseBottomToolbar) {
                updateIcon(R.drawable.ic_toolbar_bottom_expanded, R.drawable.ic_toolbar_bottom_simple)
            } else {
                updateIcon(R.drawable.ic_toolbar_top_expanded, R.drawable.ic_toolbar_top_simple)
            }
        }
    }

    private fun setupGesturesCategory(
        isSwipeToolbarToSwitchTabsVisible: Boolean,
        isSummarizationEnabled: Boolean,
        isSummarizationGestureEnabled: Boolean,
    ) {
        requirePreference<SwitchPreferenceCompat>(R.string.pref_key_website_pull_to_refresh).apply {
            isVisible = FeatureFlags.PULL_TO_REFRESH_ENABLED
            isChecked = context.components.settings.isPullToRefreshEnabledInBrowser
            onPreferenceChangeListener = SharedPreferenceUpdater()
        }
        requirePreference<SwitchPreferenceCompat>(R.string.pref_key_dynamic_toolbar).apply {
            isChecked = context.components.settings.isDynamicToolbarEnabled
            onPreferenceChangeListener = SharedPreferenceUpdater()
        }
        requirePreference<SwitchPreferenceCompat>(R.string.pref_key_swipe_toolbar_switch_tabs).apply {
            isChecked = context.components.settings.isSwipeToolbarToSwitchTabsEnabled
            isVisible = isSwipeToolbarToSwitchTabsVisible
            onPreferenceChangeListener = SharedPreferenceUpdater()
        }
        requirePreference<SwitchPreferenceCompat>(R.string.pref_key_swipe_toolbar_show_tabs).apply {
            isChecked = context.components.settings.isSwipeToolbarToShowTabsEnabled
            onPreferenceChangeListener = SharedPreferenceUpdater()
        }
        requirePreference<SwitchPreferenceCompat>(R.string.pref_key_shake_gesture_enabled).apply {
            isVisible = context.components.settings.shakeToSummarizeFeatureFlagEnabled && isSummarizationEnabled
            isChecked = isSummarizationGestureEnabled
            onPreferenceChangeListener = { _, newValue ->
                val updatedValue = (newValue as? Boolean) ?: false
                viewLifecycleOwner.lifecycleScope.launch {
                    SummarizationSettings.dataStore(requireContext()).setGestureEnabledUserStatus(updatedValue)
                }
                true
            }
        }
    }

    override fun onPreferenceTreeClick(preference: Preference): Boolean {
        when (preference.key) {
            resources.getString(R.string.pref_key_website_pull_to_refresh) -> {
                PullToRefreshInBrowser.enabled.set(requireComponents.settings.isPullToRefreshEnabledInBrowser)
            }
            resources.getString(R.string.pref_key_dynamic_toolbar) -> {
                CustomizationSettings.dynamicToolbar.set(requireComponents.settings.isDynamicToolbarEnabled)
            }
        }
        return super.onPreferenceTreeClick(preference)
    }

    override fun onConfigurationChanged(newConfig: Configuration) {
        super.onConfigurationChanged(newConfig)
        setupToolbarLayout()
        updateToolbarShortcut()
    }

    companion object {
        // Used to send telemetry data about toolbar position changes
        enum class Position {
            TOP,
            BOTTOM,
        }
    }

    private data class DynamicFeaturesStatus(
        val isSummarizationFeatureEnabled: Boolean,
        val isSummarizationGestureEnabled: Boolean,
        val isTranslationsFeatureEnabled: Boolean,
        val isTranslationsFeatureSupported: Boolean,
    )
}

/** Provides the toolbar shortcuts options for use in settings search. */
object ToolbarShortcutSettingsSearchProvider : SettingsSearchProvider {
    private val preferenceFileInformation = CustomizationPreferences

    override fun getSearchItems(context: Context) =
        listOf(
            SettingsSearchItem(
                title = context.getString(R.string.preferences_toolbar_shortcut),
                summary = "",
                preferenceKey = context.getString(R.string.pref_key_customization_category_toolbar_shortcut),
                categoryHeader = context.getString(preferenceFileInformation.categoryHeaderResourceId),
                preferenceFileInformation = preferenceFileInformation,
            )
        )
}
