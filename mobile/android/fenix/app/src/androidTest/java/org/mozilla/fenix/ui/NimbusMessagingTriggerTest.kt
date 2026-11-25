/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.ui

import mozilla.components.ExperimentalAndroidComponentsApi
import mozilla.components.concept.engine.ipprotection.CurrentAuthProvider
import mozilla.components.concept.engine.ipprotection.ServiceState
import mozilla.components.feature.ipprotection.store.state.AccountState
import mozilla.components.feature.ipprotection.store.state.AccountStatus
import mozilla.components.feature.ipprotection.store.state.Authorized
import mozilla.components.feature.ipprotection.store.state.EligibilityStatus
import mozilla.components.feature.ipprotection.store.state.IPProtectionState
import mozilla.components.service.nimbus.messaging.FxNimbusMessaging
import mozilla.components.service.nimbus.messaging.Messaging
import org.junit.Assert
import org.junit.Before
import org.junit.Rule
import org.junit.Test
import org.mozilla.experiments.nimbus.NimbusInterface
import org.mozilla.experiments.nimbus.internal.NimbusException
import org.mozilla.fenix.ext.components
import org.mozilla.fenix.helpers.FenixTestRule
import org.mozilla.fenix.helpers.HomeActivityIntentTestRule
import org.mozilla.fenix.helpers.TestHelper
import org.mozilla.fenix.messaging.CustomAttributeProvider
import org.mozilla.fenix.settings.ipprotection.toMessageContext

/**
 * Test to instantiate Nimbus and automatically test all trigger expressions shipping with the app.
 *
 * We do this as a UI test to make sure that:
 * - as much of the custom targeting and trigger attributes are recorded as possible.
 * - we can run the Rust JEXL evaluator.
 */
@OptIn(ExperimentalAndroidComponentsApi::class) // IPProtectionState
class NimbusMessagingTriggerTest {
    private lateinit var feature: Messaging
    private lateinit var nimbus: NimbusInterface

    @get:Rule(order = 0) val fenixTestRule: FenixTestRule = FenixTestRule()

    @get:Rule val activityTestRule = HomeActivityIntentTestRule.withDefaultSettingsOverrides()

    @Before
    fun setUp() {
        nimbus = TestHelper.appContext.components.nimbus.sdk
        feature = FxNimbusMessaging.features.messaging.value()
    }

    @Test
    fun testAllMessageTriggersAreValid() {
        val triggers = feature.triggers
        val customAttributes =
            CustomAttributeProvider.getCustomAttributes(TestHelper.appContext)
                .put("triggerContext", ipProtectionTriggerContext())
        val jexl = nimbus.createMessageHelper(customAttributes)

        val failed = mutableMapOf<String, String>()
        triggers.forEach { (key, expr) ->
            try {
                jexl.evalJexl(expr)
            } catch (e: NimbusException) {
                failed.put(key, expr)
            }
        }
        if (failed.isNotEmpty()) {
            Assert.fail("Expressions failed: $failed")
        }
    }

    @Test
    fun testBadTriggersAreDetected() {
        val jexl = nimbus.createMessageHelper()

        val triggers =
            mapOf(
                "Syntax error" to "|'syntax error'|",
                "Invalid identifier" to "invalid_identifier",
                "Invalid transform" to "'string'|invalid_transform",
                "Invalid interval" to "'string'|eventLastSeen('Invalid')",
            )

        triggers.forEach { (key, expr) ->
            try {
                jexl.evalJexl(expr)
                Assert.fail("$key expression failed to error: $expr")
            } catch (e: NimbusException) {
                // NOOP
            }
        }
    }

    /** VPN feature produces additional trigger context needed for this test to be added here */
    private fun ipProtectionTriggerContext(): Map<String, Any?> {
        val vpnState =
            IPProtectionState(
                eligibilityStatus = EligibilityStatus.Eligible,
                proxyStatus = Authorized.Idle,
                serviceStatus = ServiceState.Ready,
                currentAuthProvider = CurrentAuthProvider.Gpi,
                accountState = AccountState(AccountStatus.NoAccount),
            )
        return vpnState.toMessageContext()
    }
}
