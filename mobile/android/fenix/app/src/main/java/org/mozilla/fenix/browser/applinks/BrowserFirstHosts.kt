/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.browser.applinks

import androidx.core.net.toUri
import mozilla.components.support.ktx.android.net.hostWithoutCommonPrefixes
import org.mozilla.fenix.utils.Settings

/**
 * Applies the browser-first preference to a set of hosts, so their app links stay in the browser rather than open in an
 * external app. Otherwise, it will fall back to the global open links in apps behavior.
 *
 * @param settings The [Settings] holding the browser-first preferences.
 * @param featureHelper Gates the feature.
 */
class BrowserFirstHosts(
    private val settings: Settings,
    private val featureHelper: BrowserFirstFeatureHelper = DefaultBrowserFirstFeatureHelper,
) {

    /** Returns true when [url]'s host, ignoring common "www"/"m"/"mobile" prefixes, is a browser-first host. */
    fun isBrowserFirstHost(url: String): Boolean =
        (featureHelper.enabled || settings.openLinksInYoutubeFeatureEnabled) &&
            url.lowercase().toUri().hostWithoutCommonPrefixes in YOUTUBE_HOSTS

    /** Whether [url] should launch in an app, or null to defer to the global preference. */
    fun launchInApp(url: String): Boolean? = if (isBrowserFirstHost(url)) settings.shouldOpenYoutubeInApp() else null

    /** Whether [url] should prompt before launching in an app, or null to defer to the global preference. */
    fun shouldPrompt(url: String): Boolean? =
        if (isBrowserFirstHost(url)) settings.shouldPromptOpenYoutubeInApp() else null

    companion object {
        private val YOUTUBE_HOSTS =
            setOf(
                "youtube.com",
                "music.youtube.com",
                "youtu.be",
                "youtubekids.com",
            )
    }
}
