/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.settings.sitepermissions

import android.content.Intent
import android.net.Uri
import android.os.Bundle
import android.provider.Settings.ACTION_APPLICATION_DETAILS_SETTINGS
import android.text.SpannableString
import android.text.SpannableStringBuilder
import android.text.Spanned.SPAN_EXCLUSIVE_INCLUSIVE
import android.text.style.AbsoluteSizeSpan
import android.text.style.ForegroundColorSpan
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.widget.Button
import android.widget.RadioButton
import androidx.core.content.ContextCompat
import androidx.fragment.app.Fragment
import androidx.navigation.fragment.navArgs
import mozilla.components.feature.sitepermissions.SitePermissionsRules
import mozilla.components.feature.sitepermissions.SitePermissionsRules.Action.ALLOWED
import mozilla.components.feature.sitepermissions.SitePermissionsRules.Action.BLOCKED
import org.mozilla.fenix.GleanMetrics.Autoplay
import org.mozilla.fenix.R
import org.mozilla.fenix.databinding.FragmentManageSitePermissionsFeaturePhoneBinding
import org.mozilla.fenix.e2e.SystemInsetsPaddedFragment
import org.mozilla.fenix.ext.components
import org.mozilla.fenix.ext.pixelSizeFor
import org.mozilla.fenix.ext.settings
import org.mozilla.fenix.ext.showToolbar
import org.mozilla.fenix.settings.PhoneFeature
import org.mozilla.fenix.settings.PhoneFeature.AUTOPLAY_AUDIBLE
import org.mozilla.fenix.settings.PhoneFeature.AUTOPLAY_INAUDIBLE
import org.mozilla.fenix.settings.setStartCheckedIndicator
import org.mozilla.fenix.theme.ThemeManager
import org.mozilla.fenix.utils.Settings
import com.google.android.material.R as materialR

const val AUTOPLAY_BLOCK_ALL = 0
const val AUTOPLAY_BLOCK_AUDIBLE = 1
const val AUTOPLAY_ALLOW_ON_WIFI = 2
const val AUTOPLAY_ALLOW_ALL = 3

/**
 * Possible values for autoplay setting changed extra key.
 */
enum class AutoplaySettingMetricsExtraKey {
    BLOCK_CELLULAR, BLOCK_AUDIO, BLOCK_ALL, ALLOW_ALL
}

/**
 * Settings screen allowing users to manage the browser phone permission.
 */
@SuppressWarnings("TooManyFunctions")
class SitePermissionsManagePhoneFeatureFragment : Fragment(), SystemInsetsPaddedFragment {

    private val args by navArgs<SitePermissionsManagePhoneFeatureFragmentArgs>()
    private val settings by lazy { requireContext().settings() }
    private lateinit var blockedByAndroidView: View
    private var _binding: FragmentManageSitePermissionsFeaturePhoneBinding? = null
    private val binding get() = _binding!!

    override fun onCreateView(
        inflater: LayoutInflater,
        container: ViewGroup?,
        savedInstanceState: Bundle?,
    ): View {
        _binding = FragmentManageSitePermissionsFeaturePhoneBinding.inflate(inflater, container, false)

        initFirstRadio()
        initSecondRadio()
        initThirdRadio()
        initFourthRadio()
        bindBlockedByAndroidContainer()

        return binding.root
    }

    override fun onResume() {
        super.onResume()
        showToolbar(args.phoneFeature.getLabel(requireContext()))
        initBlockedByAndroidView(args.phoneFeature, blockedByAndroidView)
    }

    override fun onDestroyView() {
        super.onDestroyView()
        _binding = null
    }

    private fun initFirstRadio() {
        with(binding.askToAllowRadio) {
            if (args.phoneFeature == AUTOPLAY_AUDIBLE) {
                text = getString(R.string.preference_option_autoplay_allowed2)
                setOnClickListener {
                    saveActionInSettings(AUTOPLAY_ALLOW_ALL)
                }
                restoreState(AUTOPLAY_ALLOW_ALL)
                visibility = View.VISIBLE
            } else {
                text = getCombinedLabel(
                    getString(R.string.preference_option_phone_feature_ask_to_allow),
                    getString(R.string.phone_feature_recommended),
                )
                setOnClickListener {
                    saveActionInSettings(SitePermissionsRules.Action.ASK_TO_ALLOW)
                }
                restoreState(SitePermissionsRules.Action.ASK_TO_ALLOW)
                visibility = View.VISIBLE
            }
        }
    }

    private fun initSecondRadio() {
        with(binding.blockRadio) {
            if (args.phoneFeature == AUTOPLAY_AUDIBLE) {
                text = getCombinedLabel(
                    getString(R.string.preference_option_autoplay_allowed_wifi_only2),
                    getString(R.string.preference_option_autoplay_allowed_wifi_subtext),
                )
                setOnClickListener {
                    saveActionInSettings(AUTOPLAY_ALLOW_ON_WIFI)
                }
                restoreState(AUTOPLAY_ALLOW_ON_WIFI)
            } else {
                text = getString(R.string.preference_option_phone_feature_blocked)
                setOnClickListener {
                    saveActionInSettings(BLOCKED)
                }
                restoreState(BLOCKED)
            }
        }
    }

    private fun initThirdRadio() {
        with(binding.thirdRadio) {
            if (args.phoneFeature == AUTOPLAY_AUDIBLE) {
                visibility = View.VISIBLE
                text = getCombinedLabel(
                    getString(R.string.preference_option_autoplay_block_audio2),
                    getString(R.string.phone_feature_recommended),
                )
                setOnClickListener {
                    saveActionInSettings(AUTOPLAY_BLOCK_AUDIBLE)
                }
                restoreState(AUTOPLAY_BLOCK_AUDIBLE)
            } else if (args.phoneFeature == PhoneFeature.MEDIA_KEY_SYSTEM_ACCESS) {
                visibility = View.VISIBLE
                text = getString(R.string.preference_option_phone_feature_allowed)
                setOnClickListener {
                    saveActionInSettings(ALLOWED)
                }
                restoreState(ALLOWED)
            } else {
                visibility = View.GONE
            }
        }
    }

    private fun initFourthRadio() {
        with(binding.fourthRadio) {
            if (args.phoneFeature == AUTOPLAY_AUDIBLE) {
                visibility = View.VISIBLE
                text = getString(R.string.preference_option_autoplay_blocked3)

                setOnClickListener {
                    saveActionInSettings(AUTOPLAY_BLOCK_ALL)
                }
                restoreState(AUTOPLAY_BLOCK_ALL)
            } else {
                visibility = View.GONE
            }
        }
    }

    private fun RadioButton.restoreState(buttonAction: SitePermissionsRules.Action) {
        if (args.phoneFeature.getAction(settings) == buttonAction) {
            this.isChecked = true
            this.setStartCheckedIndicator()
        }
    }

    private fun RadioButton.restoreState(buttonAutoplaySetting: Int) {
        if (settings.getAutoplayUserSetting() == buttonAutoplaySetting) {
            this.isChecked = true
            this.setStartCheckedIndicator()
        }
    }

    private fun saveActionInSettings(action: SitePermissionsRules.Action) {
        settings.setSitePermissionsPhoneFeatureAction(args.phoneFeature, action)
    }

    /**
     * Saves the user selected autoplay setting.
     *
     * See [Settings.setAutoplayUserSetting] kdoc for an explanation of why this cannot follow the
     * same code path as other permissions.
     */
    private fun saveActionInSettings(autoplaySetting: Int) {
        settings.setAutoplayUserSetting(autoplaySetting)

        val (audible, inaudible) = when (autoplaySetting) {
            AUTOPLAY_ALLOW_ALL -> {
                ALLOWED to ALLOWED
            }
            AUTOPLAY_ALLOW_ON_WIFI -> {
                BLOCKED to BLOCKED
            }
            AUTOPLAY_BLOCK_AUDIBLE -> {
                BLOCKED to ALLOWED
            }
            AUTOPLAY_BLOCK_ALL -> {
                BLOCKED to BLOCKED
            }
            else -> return
        }

        autoplaySetting.toAutoplayMetricsExtraKey()?.let { extraKey ->
            Autoplay.settingChanged.record(Autoplay.SettingChangedExtra(extraKey))
        }

        settings.setSitePermissionsPhoneFeatureAction(AUTOPLAY_AUDIBLE, audible)
        settings.setSitePermissionsPhoneFeatureAction(AUTOPLAY_INAUDIBLE, inaudible)
        context?.components?.useCases?.sessionUseCases?.reload?.invoke()
    }

    private fun bindBlockedByAndroidContainer() {
        blockedByAndroidView = binding.root.findViewById(R.id.permissions_blocked_container)
        initSettingsButton(blockedByAndroidView)
    }

    private fun initSettingsButton(rootView: View) {
        val button = rootView.findViewById<Button>(R.id.settings_button)
        button.setOnClickListener {
            openSettings()
        }
    }

    private fun openSettings() {
        val intent = Intent(ACTION_APPLICATION_DETAILS_SETTINGS)
        val uri = Uri.fromParts("package", requireContext().packageName, null)
        intent.data = uri
        startActivity(intent)
    }

    /**
     * Returns a [CharSequence] that arranges and styles [mainText], a line break, and then [subText]
     */
    private fun getCombinedLabel(mainText: CharSequence, subText: CharSequence): CharSequence {
        val subTextSize = pixelSizeFor(R.dimen.phone_feature_label_recommended_text_size)
        val recommendedSpannable = SpannableString(subText)
        val subTextColor = ContextCompat.getColor(
            requireContext(),
            ThemeManager.resolveAttribute(materialR.attr.colorOnSurfaceVariant, requireContext()),
        )

        recommendedSpannable.setSpan(
            ForegroundColorSpan(subTextColor),
            0,
            recommendedSpannable.length,
            SPAN_EXCLUSIVE_INCLUSIVE,
        )

        recommendedSpannable.setSpan(
            AbsoluteSizeSpan(subTextSize),
            0,
            recommendedSpannable.length,
            SPAN_EXCLUSIVE_INCLUSIVE,
        )

        return with(SpannableStringBuilder()) {
            append(mainText)
            append("\n")
            append(recommendedSpannable)
            this
        }
    }

    /**
     * Returns a [AutoplaySettingMetricsExtraKey] from an AUTOPLAY setting value.
     */
    private fun Int.toAutoplayMetricsExtraKey(): String? {
        return when (this) {
            AUTOPLAY_BLOCK_ALL -> AutoplaySettingMetricsExtraKey.BLOCK_ALL.name.lowercase()
            AUTOPLAY_BLOCK_AUDIBLE -> AutoplaySettingMetricsExtraKey.BLOCK_AUDIO.name.lowercase()
            AUTOPLAY_ALLOW_ON_WIFI -> AutoplaySettingMetricsExtraKey.BLOCK_CELLULAR.name.lowercase()
            AUTOPLAY_ALLOW_ALL -> AutoplaySettingMetricsExtraKey.ALLOW_ALL.name.lowercase()
            else -> null
        }
    }
}
