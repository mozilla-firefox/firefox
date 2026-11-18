/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.onboarding

import io.mockk.mockk
import io.mockk.verify
import mozilla.components.support.test.robolectric.testContext
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import org.mozilla.fenix.utils.Settings
import org.robolectric.RobolectricTestRunner

private const val TIME_IN_MILLIS = 1759926358L

@RunWith(RobolectricTestRunner::class)
class OnboardingTermsOfServiceEventHandlerTest {

    private lateinit var eventHandler: OnboardingTermsOfServiceEventHandler
    private lateinit var telemetryRecorder: OnboardingTelemetryRecorder
    private lateinit var settings: Settings

    private var openLinkUrl: String? = null
    private var showManagePrivacyPreferencesDialogCalled = false
    private var gleanStarted = false

    @Before
    fun setup() {
        openLinkUrl = null
        showManagePrivacyPreferencesDialogCalled = false
        gleanStarted = false

        telemetryRecorder = mockk(relaxed = true)
        settings = Settings(testContext)

        eventHandler =
            OnboardingTermsOfServiceEventHandler(
                telemetryRecorder = telemetryRecorder,
                openLink = { openLinkUrl = it },
                showManagePrivacyPreferencesDialog = { showManagePrivacyPreferencesDialogCalled = true },
                settings = settings,
                startGlean = { gleanStarted = true },
                currentTimeMillis = { TIME_IN_MILLIS },
            )
    }

    @Test
    fun onTermsOfServiceLinkClicked() {
        val url = "terms-of-services"

        eventHandler.onTermsOfServiceLinkClicked(url)

        verify {
            telemetryRecorder.onTermsOfServiceLinkClick()
        }
        assertEquals(url, openLinkUrl)
    }

    @Test
    fun onPrivacyNoticeLinkClicked() {
        val url = "privacy-notice"

        eventHandler.onPrivacyNoticeLinkClicked(url)

        verify {
            telemetryRecorder.onTermsOfServicePrivacyNoticeLinkClick()
        }
        assertEquals(url, openLinkUrl)
    }

    @Test
    fun onManagePrivacyPreferencesLinkClicked() {
        eventHandler.onManagePrivacyPreferencesLinkClicked()

        verify {
            telemetryRecorder.onTermsOfServiceManagePrivacyPreferencesLinkClick()
        }
        assertTrue(showManagePrivacyPreferencesDialogCalled)
    }

    @Test
    fun onAcceptTermsButtonClicked() {
        eventHandler.onAcceptTermsButtonClicked()

        verify {
            telemetryRecorder.onTermsOfServiceManagerAcceptTermsButtonClick()
        }

        assert(settings.hasAcceptedTermsOfService)
        assertEquals(5, settings.termsOfUseAcceptedVersion)
        assertEquals(TIME_IN_MILLIS, settings.termsOfUseAcceptedTimeInMillis)
        assertTrue(gleanStarted)
    }
}
