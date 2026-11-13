/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.settings

import android.os.Bundle
import androidx.navigation.fragment.navArgs
import androidx.preference.PreferenceCategory
import androidx.preference.PreferenceFragmentCompat
import org.mozilla.fenix.R
import org.mozilla.fenix.browser.browsingmode.BrowsingMode
import org.mozilla.fenix.e2e.SystemInsetsPaddedFragment
import org.mozilla.fenix.ext.requireComponents
import org.mozilla.fenix.ext.showToolbar
import org.mozilla.fenix.utils.view.addToRadioGroup

/** Lets the user choose how open links in apps feature behaves. */
class OpenLinksInAppsFragment : PreferenceFragmentCompat(), SystemInsetsPaddedFragment {
    private lateinit var radioAlways: RadioButtonPreference
    private lateinit var radioAskBeforeOpening: RadioButtonPreference
    private lateinit var radioNever: RadioButtonPreference
    private lateinit var mediaCategory: PreferenceCategory
    private lateinit var radioYoutubeAlways: RadioButtonPreference
    private lateinit var radioYoutubeAsk: RadioButtonPreference
    private lateinit var radioYoutubeNever: RadioButtonPreference
    private val args by navArgs<OpenLinksInAppsFragmentArgs>()

    override fun onCreatePreferences(savedInstanceState: Bundle?, rootKey: String?) {
        setPreferencesFromResource(R.xml.open_links_in_apps_preferences, rootKey)

        radioAlways = requirePreference(R.string.pref_key_open_links_in_apps_always)
        radioAskBeforeOpening = requirePreference(R.string.pref_key_open_links_in_apps_ask)
        radioNever = requirePreference(R.string.pref_key_open_links_in_apps_never)

        mediaCategory = requirePreference(R.string.pref_key_media_category)
        radioYoutubeAlways = requirePreference(R.string.pref_key_open_links_in_youtube_always)
        radioYoutubeAsk = requirePreference(R.string.pref_key_open_links_in_youtube_ask)
        radioYoutubeNever = requirePreference(R.string.pref_key_open_links_in_youtube_never)

        // only show the Always option in normal browsing mode
        radioAlways.isVisible = requireComponents.settings.lastKnownMode == BrowsingMode.Normal
        radioYoutubeAlways.isVisible = requireComponents.settings.lastKnownMode == BrowsingMode.Normal
        mediaCategory.isVisible = requireComponents.settings.openLinksInYoutubeFeatureEnabled
    }

    override fun onResume() {
        super.onResume()
        showToolbar(getString(R.string.preferences_open_links_in_apps))

        setupPreferences()
        args.preferenceToScrollTo?.let {
            scrollToPreferenceWithHighlight(it)
        }
    }

    private fun setupPreferences() {
        uncheckAllRadioButtons()

        when (requireComponents.settings.openLinksInExternalApp) {
            getString(R.string.pref_key_open_links_in_apps_always) ->
                if (requireComponents.settings.lastKnownMode == BrowsingMode.Normal) {
                    radioAlways.setCheckedWithoutClickListener(true)
                } else {
                    radioAskBeforeOpening.setCheckedWithoutClickListener(true)
                }
            getString(R.string.pref_key_open_links_in_apps_ask) ->
                radioAskBeforeOpening.setCheckedWithoutClickListener(true)
            getString(R.string.pref_key_open_links_in_apps_never) -> radioNever.setCheckedWithoutClickListener(true)
        }

        radioAlways.onClickListener(::onClickAlwaysOption)
        radioAskBeforeOpening.onClickListener(::onClickAskOption)
        radioNever.onClickListener(::onClickNeverOption)

        when (requireComponents.settings.openLinksInYoutube) {
            getString(R.string.pref_key_open_links_in_youtube_always) ->
                radioYoutubeAlways.setCheckedWithoutClickListener(true)
            getString(R.string.pref_key_open_links_in_youtube_ask) ->
                radioYoutubeAsk.setCheckedWithoutClickListener(true)
            getString(R.string.pref_key_open_links_in_youtube_never) ->
                radioYoutubeNever.setCheckedWithoutClickListener(true)
        }

        radioYoutubeAlways.onClickListener(::onClickYoutubeAlwaysOption)
        radioYoutubeAsk.onClickListener(::onClickYoutubeAskOption)
        radioYoutubeNever.onClickListener(::onClickYoutubeNeverOption)

        setupRadioGroups()
    }

    private fun uncheckAllRadioButtons() {
        listOf(
                radioAlways,
                radioAskBeforeOpening,
                radioNever,
                radioYoutubeAlways,
                radioYoutubeAsk,
                radioYoutubeNever,
            )
            .forEach { it.setCheckedWithoutClickListener(false) }
    }

    private fun setupRadioGroups() {
        addToRadioGroup(
            radioAlways,
            radioAskBeforeOpening,
            radioNever,
        )
        addToRadioGroup(
            radioYoutubeAlways,
            radioYoutubeAsk,
            radioYoutubeNever,
        )
    }

    private fun onClickAlwaysOption() {
        requireComponents.settings.openLinksInExternalApp = getString(R.string.pref_key_open_links_in_apps_always)
        requireComponents.settings.openLinksInYoutube = getString(R.string.pref_key_open_links_in_youtube_always)
        radioYoutubeAlways.setCheckedWithoutClickListener(true)
    }

    private fun onClickAskOption() {
        requireComponents.settings.openLinksInExternalApp = getString(R.string.pref_key_open_links_in_apps_ask)
    }

    private fun onClickNeverOption() {
        requireComponents.settings.openLinksInExternalApp = getString(R.string.pref_key_open_links_in_apps_never)
    }

    private fun onClickYoutubeAlwaysOption() {
        requireComponents.settings.openLinksInYoutube = getString(R.string.pref_key_open_links_in_youtube_always)
    }

    private fun onClickYoutubeAskOption() {
        requireComponents.settings.openLinksInYoutube = getString(R.string.pref_key_open_links_in_youtube_ask)
    }

    private fun onClickYoutubeNeverOption() {
        requireComponents.settings.openLinksInYoutube = getString(R.string.pref_key_open_links_in_youtube_never)
    }
}
