/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.termsofuse.store

import junit.framework.TestCase.assertFalse
import junit.framework.TestCase.assertTrue
import mozilla.components.support.test.robolectric.testContext
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import org.mozilla.fenix.termsofuse.TOU_TIME_IN_MILLIS
import org.mozilla.fenix.utils.Settings
import org.robolectric.RobolectricTestRunner

@RunWith(RobolectricTestRunner::class)
class PrivacyNoticeBannerRepositoryTest {
    private lateinit var settings: Settings

    private lateinit var repository: DefaultPrivacyNoticeBannerRepository

    @Before
    fun setup() {
        settings = Settings(testContext)
        repository = DefaultPrivacyNoticeBannerRepository(settings)
    }

    @Test
    fun `WHEN updatePrivacyNoticeBannerSeenPreference is called THEN the preference is updated`() {
        repository.updatePrivacyNoticeBannerDisplayedPreference()

        assertTrue(settings.privacyNoticeBannerLastDisplayedTimeInMillis > 0)
    }

    @Test
    fun `WHEN the user has not accepted the terms of use THEN the banner should not show`() {
        settings.hasAcceptedTermsOfService = false
        settings.termsOfUseAcceptedTimeInMillis = 0
        settings.privacyNoticeBannerLastDisplayedTimeInMillis = 0

        assertFalse(repository.shouldShowPrivacyNoticeBanner())
    }

    @Test
    fun `WHEN the user has accepted the terms of use after the latest TOU update THEN the banner should not show`() {
        settings.hasAcceptedTermsOfService = true
        settings.termsOfUseAcceptedTimeInMillis = AFTER_TOU_TIME
        settings.privacyNoticeBannerLastDisplayedTimeInMillis = 0

        assertFalse(repository.shouldShowPrivacyNoticeBanner())
    }

    @Test
    fun `WHEN the user has seen the banner after the latest TOU update THEN the banner should not show`() {
        settings.hasAcceptedTermsOfService = true
        settings.termsOfUseAcceptedTimeInMillis = 0
        settings.privacyNoticeBannerLastDisplayedTimeInMillis = AFTER_TOU_TIME

        assertFalse(repository.shouldShowPrivacyNoticeBanner())
    }

    @Test
    fun `WHEN the user has accepted TOU AND there is a new update THEN the banner should show`() {
        settings.hasAcceptedTermsOfService = true
        settings.termsOfUseAcceptedTimeInMillis = 0
        settings.privacyNoticeBannerLastDisplayedTimeInMillis = 0

        assertTrue(repository.shouldShowPrivacyNoticeBanner())
    }

    @Test
    fun `WHEN the user has accepted TOU and seen the banner after the latest update THEN the banner should not show`() {
        settings.hasAcceptedTermsOfService = true
        settings.termsOfUseAcceptedTimeInMillis = AFTER_TOU_TIME
        settings.privacyNoticeBannerLastDisplayedTimeInMillis = AFTER_TOU_TIME

        assertFalse(repository.shouldShowPrivacyNoticeBanner())
    }

    companion object {
        private const val AFTER_TOU_TIME = TOU_TIME_IN_MILLIS + 1_000
    }
}
