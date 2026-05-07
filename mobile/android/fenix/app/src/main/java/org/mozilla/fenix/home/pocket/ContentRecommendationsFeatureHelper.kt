/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.home.pocket

import android.content.Context
import mozilla.components.support.locale.LocaleManager
import mozilla.components.support.locale.LocaleManager.getSystemDefault
import org.mozilla.fenix.FeatureFlags

/**
 * Utility singleton for providing helper functions related to content recommendations feature
 * flags.
 */
object ContentRecommendationsFeatureHelper {
    /**
     * List of supported content recommendations locales.
     */
    val CONTENT_RECOMMENDATIONS_SUPPORTED_LOCALE = listOf(
        "fr",
        "fr-FR",
        "es",
        "es-ES",
        "it",
        "it-IT",
        "en",
        "en-CA",
        "en-GB",
        "en-US",
        "de",
        "de-DE",
        "de-AT",
        "de-CH",
    )

    /**
     * Show Pocket sponsored stories in between Pocket recommended stories on home.
     */
    fun isPocketSponsoredStoriesFeatureEnabled(context: Context): Boolean {
        return isContentRecommendationsFeatureEnabled(context)
    }

    /**
     * Returns true if the current locale is part of the supported locales for content
     * recommendations, and false otherwise.
     */
    fun isContentRecommendationsFeatureEnabled(context: Context): Boolean {
        if (FeatureFlags.ROBOWOLF_DEBLOAT_SPONSORED) return false
        val langTag = LocaleManager.getCurrentLocale(context)
            ?.toLanguageTag() ?: getSystemDefault().toLanguageTag()
        return CONTENT_RECOMMENDATIONS_SUPPORTED_LOCALE.contains(langTag)
    }
}
