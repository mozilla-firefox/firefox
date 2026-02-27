/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.settings

import android.content.res.Configuration
import android.os.Build
import android.os.Build.VERSION.SDK_INT
import android.os.Bundle
import androidx.appcompat.app.AppCompatDelegate
import androidx.navigation.fragment.navArgs
import androidx.preference.Preference
import androidx.preference.PreferenceCategory
import androidx.preference.PreferenceFragmentCompat
import androidx.preference.SwitchPreference
import org.mozilla.fenix.FeatureFlags
import org.mozilla.fenix.GleanMetrics.AppTheme
import org.mozilla.fenix.GleanMetrics.CustomizationSettings
import org.mozilla.fenix.GleanMetrics.PullToRefreshInBrowser
import org.mozilla.fenix.GleanMetrics.ToolbarSettings
import org.mozilla.fenix.R
import org.mozilla.fenix.components.toolbar.ToolbarPosition
import org.mozilla.fenix.ext.isTallWindow
import org.mozilla.fenix.ext.isWideWindow
import org.mozilla.fenix.ext.requireComponents
import org.mozilla.fenix.ext.settings
import org.mozilla.fenix.ext.showToolbar
import org.mozilla.fenix.utils.Settings
import org.mozilla.fenix.utils.view.addToRadioGroup

/**
 * Lets the user customize the UI.
 */

@Suppress("TooManyFunctions")
class CustomizationFragment : PreferenceFragmentCompat() {
    private lateinit var radioLightTheme: RadioButtonPreference
    private lateinit var radioDarkTheme: RadioButtonPreference
    private lateinit var radioAutoBatteryTheme: RadioButtonPreference
    private lateinit var radioFollowDeviceTheme: RadioButtonPreference
    private val args by navArgs<CustomizationFragmentArgs>()

    override fun onCreatePreferences(savedInstanceState: Bundle?, rootKey: String?) {
        setPreferencesFromResource(R.xml.customization_preferences, rootKey)

        setupPreferences()
    }

    override fun onResume() {
        super.onResume()
        showToolbar(getString(R.string.preferences_customize))
        args.preferenceToScrollTo?.let {
            scrollToPreference(it)
        }
    }

    private fun setupPreferences() {
        bindFollowDeviceTheme()
        bindDarkTheme()
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
        setupGesturesCategory(isSwipeToolbarToSwitchTabsVisible = !tabletAndTabStripEnabled)
        setupAppIconCategory()
    }

    private fun updateToolbarCategoryBasedOnTabStrip(
        tabStripEnabled: Boolean,
    ) {
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
        val category = requirePreference<PreferenceCategory>(
            R.string.pref_key_customization_category_toolbar_shortcut,
        )
        val settings = requireContext().settings()
        val isExpandedToolbarEnabled = settings.shouldUseExpandedToolbar && isTallWindow() && !isWideWindow()
        val shouldShowShortcutCategory = settings.shouldShowToolbarCustomization &&
                settings.shouldUseComposableToolbar &&
                settings.toolbarRedesignEnabled

        category.isVisible = shouldShowShortcutCategory
        if (shouldShowShortcutCategory) {
            val shortcutPreference = if (isExpandedToolbarEnabled) {
                ToolbarExpandedShortcutPreference(requireContext()).apply {
                    key = getString(R.string.pref_key_toolbar_expanded_shortcut)
                    layoutResource = R.layout.preference_toolbar_shortcut
                }
            } else {
                ToolbarSimpleShortcutPreference(requireContext()).apply {
                    key = getString(R.string.pref_key_toolbar_simple_shortcut)
                    layoutResource = R.layout.preference_toolbar_shortcut
                }
            }
            category.apply {
                removeAll()
                addPreference(shortcutPreference)
                val shortcutOptions = shortcutPreference.getShortcutOptions()
                shortcutOptions.forEach(::addPreference)
            }
        }
    }

    private fun setupRadioGroups() {
        addToRadioGroup(
            radioLightTheme,
            radioDarkTheme,
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
            AppTheme.darkThemeSelected.record(
                AppTheme.DarkThemeSelectedExtra(
                    "SETTINGS",
                ),
            )
            setNewTheme(AppCompatDelegate.MODE_NIGHT_YES)
        }
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
            ToolbarSettings.changedPosition.record(
                ToolbarSettings.ChangedPositionExtra(
                    Position.TOP.name,
                ),
            )

            updateToolbarLayoutIcons()
        }

        val bottomPreference = requirePreference<RadioButtonPreference>(R.string.pref_key_toolbar_bottom)
        bottomPreference.onClickListener {
            ToolbarSettings.changedPosition.record(
                ToolbarSettings.ChangedPositionExtra(
                    Position.BOTTOM.name,
                ),
            )

            updateToolbarLayoutIcons()
        }

        val toolbarPosition = requireContext().settings().toolbarPosition
        topPreference.setCheckedWithoutClickListener(toolbarPosition == ToolbarPosition.TOP)
        bottomPreference.setCheckedWithoutClickListener(toolbarPosition == ToolbarPosition.BOTTOM)

        addToRadioGroup(topPreference, bottomPreference)
    }

    private fun setupTabStripCategory() {
        val tabStripSwitch = requirePreference<SwitchPreference>(R.string.pref_key_tab_strip_show)
        val context = requireContext()

        tabStripSwitch.isChecked = Settings(requireContext()).isTabStripEnabled

        tabStripSwitch.setOnPreferenceChangeListener { _, newValue ->
            val enabled = newValue as Boolean
            context.settings().isTabStripEnabled = enabled
            updateToolbarCategoryBasedOnTabStrip(enabled)
            setupToolbarLayout()
            true
        }
    }

    private fun setupToolbarLayout() {
        val settings = requireContext().settings()
        (requirePreference(R.string.pref_key_customization_category_toolbar_layout) as PreferenceCategory).apply {
            isVisible = settings.shouldUseComposableToolbar &&
                    settings.toolbarRedesignEnabled && isTallWindow() && !isWideWindow()
        }

        val layoutToggle = requirePreference<ToggleRadioButtonPreference>(R.string.pref_key_toolbar_expanded)
        layoutToggle.setOnToggleChanged {
            updateToolbarShortcut()
        }
        updateToolbarLayoutIcons()
    }

    private fun updateToolbarLayoutIcons() {
        (requirePreference(R.string.pref_key_toolbar_expanded) as ToggleRadioButtonPreference).apply {
            if (requireContext().settings().shouldUseBottomToolbar) {
                updateIcon(R.drawable.ic_toolbar_bottom_expanded, R.drawable.ic_toolbar_bottom_simple)
            } else {
                updateIcon(R.drawable.ic_toolbar_top_expanded, R.drawable.ic_toolbar_top_simple)
            }
        }
    }

    private fun setupGesturesCategory(isSwipeToolbarToSwitchTabsVisible: Boolean) {
        requirePreference<SwitchPreference>(R.string.pref_key_website_pull_to_refresh).apply {
            isVisible = FeatureFlags.PULL_TO_REFRESH_ENABLED
            isChecked = context.settings().isPullToRefreshEnabledInBrowser
            onPreferenceChangeListener = SharedPreferenceUpdater()
        }
        requirePreference<SwitchPreference>(R.string.pref_key_dynamic_toolbar).apply {
            isChecked = context.settings().isDynamicToolbarEnabled
            onPreferenceChangeListener = SharedPreferenceUpdater()
        }
        requirePreference<SwitchPreference>(R.string.pref_key_swipe_toolbar_switch_tabs).apply {
            isChecked = context.settings().isSwipeToolbarToSwitchTabsEnabled
            isVisible = isSwipeToolbarToSwitchTabsVisible
            onPreferenceChangeListener = SharedPreferenceUpdater()
        }
        requirePreference<SwitchPreference>(R.string.pref_key_shake_gesture_enabled).apply {
            isVisible = context.settings().shakeToSummarizeFeatureFlagEnabled &&
                    context.settings().shakeToSummarizeFeatureUserPreference
            isChecked = context.settings().shakeGestureEnabled
            onPreferenceChangeListener = SharedPreferenceUpdater()
        }
    }

    private fun setupAppIconCategory() {
        requirePreference<PreferenceCategory>(R.string.pref_key_customization_category_app_icon).apply {
           isVisible = context.settings().appIconSelection
        }
    }

    override fun onPreferenceTreeClick(preference: Preference): Boolean {
        when (preference.key) {
            resources.getString(R.string.pref_key_website_pull_to_refresh) -> {
                PullToRefreshInBrowser.enabled.set(requireContext().settings().isPullToRefreshEnabledInBrowser)
            }
            resources.getString(R.string.pref_key_dynamic_toolbar) -> {
                CustomizationSettings.dynamicToolbar.set(requireContext().settings().isDynamicToolbarEnabled)
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
        enum class Position { TOP, BOTTOM }
    }
}
