/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.onboarding.continuous

import mozilla.components.service.nimbus.messaging.FxNimbusMessaging
import mozilla.components.service.nimbus.messaging.Messaging
import mozilla.components.support.test.assertUnused
import mozilla.components.support.test.robolectric.testContext
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import org.mozilla.experiments.nimbus.NimbusMessagingHelperInterface
import org.mozilla.experiments.nimbus.internal.NimbusException
import org.mozilla.fenix.utils.Settings
import org.robolectric.RobolectricTestRunner

private const val FINISHED_JEXL = "finished"

@RunWith(RobolectricTestRunner::class)
class ContinuousOnboardingProgressTest {
    private lateinit var settings: Settings
    private val evaluated = mutableListOf<String>()

    @Before
    fun setUp() {
        settings = Settings(testContext)
        settings.continuousOnboardingFeatureEnabled = true
        FxNimbusMessaging.features.messaging.withCachedValue(Messaging(triggers = mapOf("CO_FINISHED" to FINISHED_JEXL)))
    }

    @Test
    fun `GIVEN continuous onboarding is disabled THEN it is not in progress`() {
        settings.continuousOnboardingFeatureEnabled = false
        settings.continuousOnboardingViaMessaging = true

        assertFalse(settings.isContinuousOnboardingInProgress { jexlHelper(finished = false) })
        assertTrue(evaluated.isEmpty())
    }

    @Test
    fun `GIVEN an install on ContinuousOnboardingFeature THEN progress follows the day 7 timestamp`() {
        assertTrue(settings.isContinuousOnboardingInProgress { jexlHelper(finished = true) })

        settings.seventhDayOnboardingCompletedTimestamp = 1L

        assertFalse(settings.isContinuousOnboardingInProgress { jexlHelper(finished = false) })
        assertTrue(evaluated.isEmpty())
    }

    @Test
    fun `GIVEN an install on the Messaging System THEN progress follows the finished trigger`() {
        settings.continuousOnboardingViaMessaging = true
        settings.seventhDayOnboardingCompletedTimestamp = 1L

        assertTrue(settings.isContinuousOnboardingInProgress { jexlHelper(finished = false) })
        assertFalse(settings.isContinuousOnboardingInProgress { jexlHelper(finished = true) })
        assertEquals(listOf(FINISHED_JEXL, FINISHED_JEXL), evaluated)
    }

    @Test
    fun `GIVEN an install on the Messaging System WHEN the trigger can't be evaluated THEN it is not in progress`() {
        settings.continuousOnboardingViaMessaging = true

        assertFalse(settings.isContinuousOnboardingInProgress { jexlHelper(finished = null) })

        FxNimbusMessaging.features.messaging.withCachedValue(Messaging())

        assertFalse(settings.isContinuousOnboardingInProgress { jexlHelper(finished = false) })
    }

    private fun jexlHelper(finished: Boolean?) =
        object : NimbusMessagingHelperInterface {
            override fun evalJexl(expression: String): Boolean {
                evaluated += expression
                return finished ?: throw NimbusException.EvaluationException("failed")
            }

            override fun evalJexlDebug(expression: String) = assertUnused()

            override fun getUuid(template: String) = assertUnused()

            override fun stringFormat(template: String, uuid: String?) = assertUnused()
        }
}
