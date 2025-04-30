/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.settings

import android.content.Context
import android.content.pm.PackageInfo
import android.content.pm.PackageManager
import io.mockk.every
import io.mockk.mockk
import org.junit.Assert.assertEquals
import org.junit.Test
import java.util.Locale

class SupportUtilsTest {

    @Test
    fun getSumoURLForTopic() {
        assertEquals(
            "https://support.mozilla.org/1/mobile/1.6/Android/en-US/common-myths-about-private-browsing",
            SupportUtils.getSumoURLForTopic(
                mockContext("1.6"),
                SupportUtils.SumoTopic.PRIVATE_BROWSING_MYTHS,
                Locale.Builder().setLanguage("en").setRegion("US").build(),
            ),
        )
        assertEquals(
            "https://support.mozilla.org/1/mobile/20/Android/fr/tracking-protection-firefox-android",
            SupportUtils.getSumoURLForTopic(
                mockContext("2 0"),
                SupportUtils.SumoTopic.TRACKING_PROTECTION,
                Locale.forLanguageTag("fr"),
            ),
        )
        assertEquals(
            "https://www.mozilla.org/firefox/android/notes",
            SupportUtils.WHATS_NEW_URL,
        )
    }

    @Test
    fun getGenericSumoURLForTopic() {
        assertEquals(
            "https://support.mozilla.org/en-GB/kb/faq-android",
            SupportUtils.getGenericSumoURLForTopic(SupportUtils.SumoTopic.HELP, Locale.Builder().setLanguage("en").setRegion("GB").build()),
        )
        assertEquals(
            "https://support.mozilla.org/de/kb/your-rights",
            SupportUtils.getGenericSumoURLForTopic(SupportUtils.SumoTopic.YOUR_RIGHTS, Locale.forLanguageTag("de")),
        )
    }

    @Test
    fun getMozillaPageUrl() {
        assertEquals(
            "https://www.mozilla.org/en-US/about/manifesto/",
            SupportUtils.getMozillaPageUrl(SupportUtils.MozillaPage.MANIFESTO, Locale.Builder().setLanguage("en").setRegion("US").build()),
        )
        assertEquals(
            "https://www.mozilla.org/zh/privacy/firefox/",
            SupportUtils.getMozillaPageUrl(SupportUtils.MozillaPage.PRIVATE_NOTICE, Locale.forLanguageTag("zh")),
        )
    }

    private fun mockContext(versionName: String): Context {
        val context: Context = mockk()
        val packageManager: PackageManager = mockk()
        val packageInfo = PackageInfo()

        every { context.packageName } returns "org.mozilla.fenix"
        every { context.packageManager } returns packageManager
        @Suppress("DEPRECATION")
        every { packageManager.getPackageInfo("org.mozilla.fenix", 0) } returns packageInfo
        packageInfo.versionName = versionName

        return context
    }
}
