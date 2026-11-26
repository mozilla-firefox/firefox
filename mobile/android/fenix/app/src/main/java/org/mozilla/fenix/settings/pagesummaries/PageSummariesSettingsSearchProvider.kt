/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.settings.pagesummaries

import android.content.Context
import mozilla.components.feature.summarize.R as summariesR
import org.mozilla.fenix.settings.settingssearch.PreferenceFileInformation
import org.mozilla.fenix.settings.settingssearch.SettingsSearchItem
import org.mozilla.fenix.settings.settingssearch.SettingsSearchProvider

/** [SettingsSearchProvider] for making "Page summaries" discoverable in settings search */
object PageSummariesSettingsSearchProvider : SettingsSearchProvider {

    private val preferenceFileInformation = PreferenceFileInformation.PageSummariesPreferences

    override fun getSearchItems(context: Context): List<SettingsSearchItem> {
        return buildList {
            add(
                SettingsSearchItem(
                    title = context.getString(summariesR.string.mozac_summarize_settings_summarize_pages),
                    summary = context.getString(summariesR.string.mozac_summarize_settings_summarize_pages_cloud),
                    preferenceKey = Section.Feature.key,
                    categoryHeader = context.getString(preferenceFileInformation.categoryHeaderResourceId),
                    preferenceFileInformation = preferenceFileInformation,
                )
            )
            add(
                SettingsSearchItem(
                    title = context.getString(summariesR.string.mozac_summarize_settings_shake_to_summarize),
                    summary =
                        context.getString(summariesR.string.mozac_summarize_settings_shake_to_summarize_description),
                    preferenceKey = Section.Gestures.key,
                    categoryHeader = context.getString(preferenceFileInformation.categoryHeaderResourceId),
                    preferenceFileInformation = preferenceFileInformation,
                )
            )
        }
    }

    /** Preference sections for the "Page summaries" settings */
    private sealed interface Section {
        val key: String

        /** The page summaries feature section */
        data object Feature : Section {
            override val key: String
                get() = "PAGE_SUMMARIES_FEATURE"
        }

        /** The page summaries gestures section */
        data object Gestures : Section {
            override val key: String
                get() = "PAGE_SUMMARIES_GESTURES"
        }
    }
}
