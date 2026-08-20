/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.helpers

import org.mozilla.fenix.helpers.DataGenerationHelper.getSponsoredShortcutTitle

object Constants {

    const val GMAIL_APP_NAME = "Gmail"

    // Tag used for logging
    const val TAG = "MozUITestLog"

    // Device or AVD requires a Google Services Android OS installation
    object PackageName {
        const val GOOGLE_PLAY_SERVICES = "com.android.vending"
        const val GOOGLE_APPS_PHOTOS = "com.google.android.apps.photos"
        const val GOOGLE_QUICK_SEARCH = "com.google.android.googlequicksearchbox"
        const val GOOGLE_DOCS = "com.google.android.apps.docs"
        const val YOUTUBE_APP = "com.google.android.youtube"
        const val GMAIL_APP = "com.google.android.gm"
        const val PHONE_APP = "com.google.android.dialer"
        const val ANDROID_SETTINGS = "com.android.settings"
        const val PRINT_SPOOLER = "com.android.printspooler"
        const val PIXEL_LAUNCHER = "com.google.android.apps.nexuslauncher"
        const val DOCUMENTS_UI = "com.google.android.documentsui"
    }

    const val SPEECH_RECOGNITION = "android.speech.action.RECOGNIZE_SPEECH"
    const val STORIES_UTM_PARAM = "utm_source=firefox-newtab-en-us"
    const val LONG_CLICK_DURATION: Long = 5000
    const val LISTS_MAXSWIPES: Int = 3
    const val RETRY_COUNT = 3

    val searchEngineCodes =
        mapOf(
            "Google" to "client=firefox-b-m",
            "Bing" to "MOZB&form=MOZMBA&q=firefox",
            "DuckDuckGo" to "t=fpas",
        )

    val firstSponsoredShortcutTitle by lazy { getSponsoredShortcutTitle(2) }
    val secondSponsoredShortcutTitle by lazy { getSponsoredShortcutTitle(3) }

    // Expected for en-us defaults
    val defaultTopSitesList by lazy {
        mapOf(
            "Google" to "Google",
            "First sponsored shortcut" to firstSponsoredShortcutTitle,
            "Second sponsored shortcut" to secondSponsoredShortcutTitle,
            "Wikipedia" to "Wikipedia",
        )
    }

    // Notes:
    // "Ghostery" - not included in the list because of the name discrepancy between the recommended list and the
    // install confirmation prompt
    // "AdGuard AdBlocker" - not included in the list because of the inconsistencies caused by the new tab that is
    // opened after installation
    // "Video Background Play Fix", "FoxyProxy" and "Google Search Fixer"
    // - not included because instead of the add-on name the main menu extensions button displays "No extensions
    // enabled"
    // and no name is being displayed in the installed "Extensions" list, only the add-on icon
    val recommendedAddons =
        listOf(
            "uBlock Origin",
            "Privacy Badger",
            "Dark Reader",
            "Bitwarden",
            "NoScript",
            "Search by Image",
            "ClearURLs",
            "Firefox Relay",
            "Read Aloud: A Text to Speech Voice Reader",
            "Decentraleyes",
            "YouTube High Definition",
            "Web Archives",
            "Tomato Clock",
        )
}
