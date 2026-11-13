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
import org.mozilla.fenix.utils.Settings
import org.robolectric.RobolectricTestRunner

@RunWith(RobolectricTestRunner::class)
class PrivacyNoticeBannerRepositoryTest {
    private lateinit var settings: Settings

    private lateinit var repository: DefaultPrivacyNoticeBannerRepository

    private var now = AFTER_NEW_TOU_TIME

    @Before
    fun setup() {
        settings = Settings(testContext)
        repository =
            DefaultPrivacyNoticeBannerRepository(
                settings = settings,
                currentTimeMillis = { now },
                termsOfUsePublishedTimes = listOf(OLD_TOU_TIME, NEW_TOU_TIME),
            )
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
        settings.termsOfUseAcceptedTimeInMillis = AFTER_NEW_TOU_TIME
        settings.privacyNoticeBannerLastDisplayedTimeInMillis = 0

        assertFalse(repository.shouldShowPrivacyNoticeBanner())
    }

    @Test
    fun `WHEN the user has seen the banner after the latest TOU update THEN the banner should not show`() {
        settings.hasAcceptedTermsOfService = true
        settings.termsOfUseAcceptedTimeInMillis = 0
        settings.privacyNoticeBannerLastDisplayedTimeInMillis = AFTER_NEW_TOU_TIME

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
        settings.termsOfUseAcceptedTimeInMillis = AFTER_NEW_TOU_TIME
        settings.privacyNoticeBannerLastDisplayedTimeInMillis = AFTER_NEW_TOU_TIME

        assertFalse(repository.shouldShowPrivacyNoticeBanner())
    }

    @Test
    fun `WHEN no published date has passed THEN the banner should not show`() {
        now = OLD_TOU_TIME - 1_000
        settings.hasAcceptedTermsOfService = true
        settings.termsOfUseAcceptedTimeInMillis = 0
        settings.privacyNoticeBannerLastDisplayedTimeInMillis = 0

        assertFalse(repository.shouldShowPrivacyNoticeBanner())
    }

    @Test
    fun `GIVEN the user accepted TOU between the two dates WHEN the new date has not passed THEN the banner should not show`() {
        now = NEW_TOU_TIME - 1_000
        settings.hasAcceptedTermsOfService = true
        settings.termsOfUseAcceptedTimeInMillis = BETWEEN_TOU_TIMES
        settings.privacyNoticeBannerLastDisplayedTimeInMillis = 0

        assertFalse(repository.shouldShowPrivacyNoticeBanner())
    }

    @Test
    fun `GIVEN the user accepted TOU between the two dates WHEN the new date is reached THEN the banner should show`() {
        now = NEW_TOU_TIME
        settings.hasAcceptedTermsOfService = true
        settings.termsOfUseAcceptedTimeInMillis = BETWEEN_TOU_TIMES
        settings.privacyNoticeBannerLastDisplayedTimeInMillis = 0

        assertTrue(repository.shouldShowPrivacyNoticeBanner())
    }

    @Test
    fun `GIVEN the user saw the banner between the two dates WHEN the new date has passed THEN the banner should show`() {
        settings.hasAcceptedTermsOfService = true
        settings.termsOfUseAcceptedTimeInMillis = 0
        settings.privacyNoticeBannerLastDisplayedTimeInMillis = BETWEEN_TOU_TIMES

        assertTrue(repository.shouldShowPrivacyNoticeBanner())
    }

    @Test
    fun `WHEN the published dates are not in order THEN the most recent passed date is used`() {
        repository =
            DefaultPrivacyNoticeBannerRepository(
                settings = settings,
                currentTimeMillis = { now },
                termsOfUsePublishedTimes = listOf(NEW_TOU_TIME, OLD_TOU_TIME),
            )
        settings.hasAcceptedTermsOfService = true
        settings.termsOfUseAcceptedTimeInMillis = BETWEEN_TOU_TIMES
        settings.privacyNoticeBannerLastDisplayedTimeInMillis = 0

        assertTrue(repository.shouldShowPrivacyNoticeBanner())
    }

    companion object {
        private const val OLD_TOU_TIME = 1_000_000L
        private const val BETWEEN_TOU_TIMES = 1_500_000L
        private const val NEW_TOU_TIME = 2_000_000L
        private const val AFTER_NEW_TOU_TIME = NEW_TOU_TIME + 1_000
    }
}
