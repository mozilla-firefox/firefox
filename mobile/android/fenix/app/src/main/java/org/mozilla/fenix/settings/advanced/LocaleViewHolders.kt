/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.settings.advanced

import android.view.View
import androidx.annotation.VisibleForTesting
import androidx.core.view.isVisible
import androidx.recyclerview.widget.RecyclerView
import java.util.Locale
import org.mozilla.fenix.R
import org.mozilla.fenix.databinding.LocaleSettingsItemBinding
import org.mozilla.fenix.utils.LocaleUtils

class LocaleViewHolder(
    view: View,
    selectedLocale: Locale,
    private val interactor: LocaleSettingsViewInteractor,
    localeSelectionChecker: LocaleSelectionChecker,
) : BaseLocaleViewHolder(view, selectedLocale, localeSelectionChecker) {

    private val binding = LocaleSettingsItemBinding.bind(view)

    override fun bind(locale: Locale) {
        if (locale.language == "zh") {
            bindChineseLocale(locale)
        } else {
            // Capitalisation is done using the rules of the appropriate locale (endonym and exonym).
            binding.localeTitleText.text = LocaleUtils.getDisplayName(locale)
            // Show the given locale using the device locale for the subtitle.
            binding.localeSubtitleText.text = locale.getProperDisplayName()
        }
        binding.localeSelectedIcon.isVisible = isCurrentLocaleSelected(locale, isDefault = false)

        itemView.setOnClickListener {
            interactor.onLocaleSelected(locale)
        }
    }

    private fun bindChineseLocale(locale: Locale) {
        if (locale.country == "CN") {
            binding.localeTitleText.text = Locale.forLanguageTag("zh-Hans").getDisplayName(locale).capitalize(locale)
            binding.localeSubtitleText.text =
                Locale.forLanguageTag("zh-Hans").displayName.capitalize(Locale.getDefault())
        } else if (locale.country == "TW") {
            binding.localeTitleText.text = Locale.forLanguageTag("zh-Hant").getDisplayName(locale).capitalize(locale)
            binding.localeSubtitleText.text =
                Locale.forLanguageTag("zh-Hant").displayName.capitalize(Locale.getDefault())
        }
    }
}

class SystemLocaleViewHolder(
    view: View,
    selectedLocale: Locale,
    private val interactor: LocaleSettingsViewInteractor,
    localeSelectionChecker: LocaleSelectionChecker,
) : BaseLocaleViewHolder(view, selectedLocale, localeSelectionChecker) {

    private val binding = LocaleSettingsItemBinding.bind(view)

    override fun bind(locale: Locale) {
        binding.localeTitleText.text = itemView.context.getString(R.string.default_locale_text)
        if (locale.script == "Hant") {
            binding.localeSubtitleText.text =
                Locale.forLanguageTag("zh-Hant").displayName.capitalize(Locale.getDefault())
        } else if (locale.script == "Hans") {
            binding.localeSubtitleText.text =
                Locale.forLanguageTag("zh-Hans").displayName.capitalize(Locale.getDefault())
        } else {
            // Use the device locale for the system locale subtitle.
            binding.localeSubtitleText.text = locale.getDisplayName(locale).capitalize(locale)
        }
        binding.localeSelectedIcon.isVisible = isCurrentLocaleSelected(locale, isDefault = true)
        itemView.setOnClickListener {
            interactor.onDefaultLocaleSelected()
        }
    }
}

abstract class BaseLocaleViewHolder(
    view: View,
    private val selectedLocale: Locale,
    private val localeSelectionChecker: LocaleSelectionChecker,
) : RecyclerView.ViewHolder(view) {

    @VisibleForTesting(otherwise = VisibleForTesting.PROTECTED)
    internal fun isCurrentLocaleSelected(locale: Locale, isDefault: Boolean): Boolean {
        val isSystemDefaultSelected = localeSelectionChecker.isDefaultLocaleSelected(itemView.context)

        return if (isDefault) {
            locale == selectedLocale && isSystemDefaultSelected
        } else {
            locale == selectedLocale && !isSystemDefaultSelected
        }
    }

    abstract fun bind(locale: Locale)
}

/** Similar to Kotlin's capitalize with locale parameter, but that method is currently experimental */
private fun String.capitalize(locale: Locale): String {
    return substring(0, 1).uppercase(locale) + substring(1)
}

/** Returns the locale in the selected language, with fallback to English name */
private fun Locale.getProperDisplayName(): String {
    val displayName = this.displayName.capitalize(Locale.getDefault())
    if (displayName.equals(this.toString(), ignoreCase = true)) {
        return LocaleUtils.LOCALE_TO_DISPLAY_ENGLISH_NAME_MAP[this.toString()] ?: displayName
    }
    return displayName
}
