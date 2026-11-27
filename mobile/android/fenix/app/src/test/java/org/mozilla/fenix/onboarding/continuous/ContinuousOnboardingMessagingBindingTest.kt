/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.onboarding.continuous

import android.app.Activity
import android.app.role.RoleManager
import android.content.Intent
import android.view.ViewGroup
import androidx.activity.result.ActivityResultLauncher
import androidx.activity.result.contract.ActivityResultContract
import androidx.appcompat.app.AppCompatActivity
import androidx.compose.ui.platform.ComposeView
import androidx.core.app.ActivityOptionsCompat
import io.mockk.every
import io.mockk.mockk
import io.mockk.verify
import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.test.StandardTestDispatcher
import kotlinx.coroutines.test.runTest
import mozilla.components.ExperimentalAndroidComponentsApi
import mozilla.components.concept.engine.ipprotection.CurrentAuthProvider
import mozilla.components.concept.engine.ipprotection.ServiceState
import mozilla.components.feature.ipprotection.store.IPProtectionAction
import mozilla.components.feature.ipprotection.store.IPProtectionStore
import mozilla.components.feature.ipprotection.store.state.AccountStatus
import mozilla.components.feature.ipprotection.store.state.EligibilityStatus
import mozilla.components.feature.ipprotection.store.state.IPProtectionState
import mozilla.components.lib.state.Middleware
import mozilla.components.service.nimbus.messaging.Message
import mozilla.components.service.nimbus.messaging.MessageData
import mozilla.components.service.nimbus.messaging.NimbusMessagingControllerInterface
import mozilla.components.service.nimbus.messaging.StyleData
import mozilla.components.support.base.android.NotificationsDelegate
import mozilla.components.support.test.robolectric.testContext
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import org.mozilla.experiments.nimbus.NimbusEventStore
import org.mozilla.experiments.nimbus.internal.NimbusException
import org.mozilla.fenix.components.AppStore
import org.mozilla.fenix.components.appstate.AppAction
import org.mozilla.fenix.components.appstate.AppAction.MessagingAction
import org.mozilla.fenix.components.appstate.AppState
import org.mozilla.fenix.helpers.lifecycle.TestLifecycleOwner
import org.mozilla.fenix.ipprotection.FakeIPProtectionPromptRepository
import org.mozilla.fenix.ipprotection.store.IPProtectionPromptState.PromptDetails
import org.mozilla.fenix.messaging.FenixMessageSurfaceId
import org.mozilla.fenix.nimbus.FxNimbus
import org.mozilla.fenix.nimbus.IpProtection
import org.mozilla.fenix.utils.Settings
import org.robolectric.Robolectric
import org.robolectric.RobolectricTestRunner
import org.robolectric.Shadows.shadowOf

private const val CONTINUOUS_ONBOARDING_DIALOG_TAG = "continuous_onboarding_dialog"

@RunWith(RobolectricTestRunner::class)
@OptIn(ExperimentalAndroidComponentsApi::class)
class ContinuousOnboardingMessagingBindingTest {
    private val testDispatcher = StandardTestDispatcher()

    private val actions = mutableListOf<AppAction>()
    private val events: NimbusEventStore = mockk(relaxed = true)
    private val messaging: NimbusMessagingControllerInterface = mockk(relaxed = true)
    private val notificationsDelegate: NotificationsDelegate = mockk(relaxed = true)
    private val telemetry: ContinuousOnboardingMessageTelemetry = mockk(relaxed = true)
    private val launcher = FakeActivityResultLauncher()
    private val ipProtectionPromptRepository = FakeIPProtectionPromptRepository()
    private val ipProtectionPrompts = mutableListOf<PromptDetails>()
    private var syncSignInNavigations = 0

    private lateinit var activity: Activity
    private lateinit var settings: Settings
    private lateinit var appStore: AppStore
    private lateinit var ipProtectionStore: IPProtectionStore
    private lateinit var binding: ContinuousOnboardingMessagingBinding

    @Before
    fun setup() {
        FxNimbus.features.ipProtection.withCachedValue(
            IpProtection(promoDeadline = "2000-01-01", dataLimitGigabyte = 50)
        )
        activity = Robolectric.buildActivity(AppCompatActivity::class.java).create().get()
        settings = Settings(testContext)
        settings.continuousOnboardingFeatureEnabled = true
        val captureActions: Middleware<AppState, AppAction> = { _, next, action ->
            actions += action
            next(action)
        }
        appStore = AppStore(middlewares = listOf(captureActions))
        ipProtectionStore =
            IPProtectionStore(
                initialState =
                    IPProtectionState(
                        serviceStatus = ServiceState.Unauthenticated,
                        currentAuthProvider = CurrentAuthProvider.Fxa,
                    )
            )
        every { notificationsDelegate.hasPostNotificationsPermission() } returns false
        every { events.recordEventOrThrow(any(), any()) } returns CompletableDeferred(Unit)
        binding =
            ContinuousOnboardingMessagingBinding(
                activity = activity,
                launcher = launcher,
                dependencies =
                    ContinuousOnboardingMessagingDependencies(
                        settings = settings,
                        appStore = appStore,
                        messaging = messaging,
                        events = events,
                        notificationsDelegate = notificationsDelegate,
                        ipProtectionStore = ipProtectionStore,
                        ipProtectionPromptRepository = ipProtectionPromptRepository,
                        crashReporter = mockk(relaxed = true),
                    ),
                telemetry = telemetry,
                navigateToSyncSignIn = { syncSignInNavigations++ },
                navigateToIpProtection = { ipProtectionPrompts += it },
                mainDispatcher = testDispatcher,
            )
    }

    @Test
    fun `WHEN resumed THEN the continuous onboarding surface is evaluated once`() {
        binding.onResume(TestLifecycleOwner())

        assertEquals(listOf(FenixMessageSurfaceId.CONTINUOUS_ONBOARDING), evaluations().map { it.surface })
    }

    @Test
    fun `GIVEN nothing can be asked for WHEN evaluating THEN the trigger context says so`() {
        settings.continuousOnboardingFeatureEnabled = false
        every { notificationsDelegate.hasPostNotificationsPermission() } returns true
        ipProtectionPromptRepository.canShowIPProtectionPrompt = false
        ipProtectionStore.dispatch(
            IPProtectionAction.EligibilityAction.EligibilityChanged(EligibilityStatus.UnsupportedRegion)
        )

        binding.onResume(TestLifecycleOwner())

        assertEquals(
            mapOf(
                "continuous_onboarding" to
                    mapOf(
                        "enabled" to false,
                        "default_browser_role_requestable" to false,
                        "notification_permission_requestable" to false,
                        "vpn_eligible" to false,
                        "vpn_ineligible" to true,
                        "vpn_prompt_allowed" to false,
                    )
            ),
            evaluations().first().triggerContext,
        )
    }

    @Test
    fun `GIVEN everything can be asked for WHEN evaluating THEN the trigger context says so`() {
        makeDefaultBrowserRoleRequestable()
        ipProtectionStore.dispatch(IPProtectionAction.EligibilityAction.EligibilityChanged(EligibilityStatus.Eligible))

        binding.onResume(TestLifecycleOwner())

        assertEquals(
            mapOf(
                "continuous_onboarding" to
                    mapOf(
                        "enabled" to true,
                        "default_browser_role_requestable" to true,
                        "notification_permission_requestable" to true,
                        "vpn_eligible" to true,
                        "vpn_ineligible" to false,
                        "vpn_prompt_allowed" to true,
                    )
            ),
            evaluations().first().triggerContext,
        )
    }

    @Test
    fun `GIVEN IP Protection eligibility is unknown WHEN evaluating THEN the user is neither eligible nor ineligible`() {
        binding.onResume(TestLifecycleOwner())

        val context = evaluations().first().triggerContext?.get("continuous_onboarding") as Map<*, *>
        assertEquals(false, context["vpn_eligible"])
        assertEquals(false, context["vpn_ineligible"])
    }

    @Test
    fun `GIVEN no messages have been restored WHEN started THEN they are restored`() =
        runTest(testDispatcher) {
            binding.start()

            assertTrue(MessagingAction.Restore in actions)
            binding.stop()
        }

    @Test
    fun `GIVEN messages have been restored WHEN started THEN the surface is evaluated without restoring again`() =
        runTest(testDispatcher) {
            appStore.dispatch(MessagingAction.UpdateMessages(listOf(syncCardMessage())))

            binding.start()
            testDispatcher.scheduler.advanceUntilIdle()

            assertFalse(MessagingAction.Restore in actions)
            assertTrue(evaluations().isNotEmpty())
            binding.stop()
        }

    @Test
    fun `WHEN IP Protection eligibility changes THEN the surface is evaluated again`() =
        runTest(testDispatcher) {
            binding.start()
            testDispatcher.scheduler.advanceUntilIdle()
            actions.clear()

            ipProtectionStore.dispatch(
                IPProtectionAction.EligibilityAction.EligibilityChanged(EligibilityStatus.Ineligible)
            )
            testDispatcher.scheduler.advanceUntilIdle()

            assertEquals(listOf(FenixMessageSurfaceId.CONTINUOUS_ONBOARDING), evaluations().map { it.surface })
            binding.stop()
        }

    @Test
    fun `WHEN a stage-completed message is selected THEN it is recorded as displayed with its telemetry`() =
        runTest(testDispatcher) {
            val message = message("co-day-5-skipped")

            present(message)

            assertTrue(MessagingAction.MessageDisplayed(message) in actions)
            verify(exactly = 1) { events.recordEvent(count = 1, eventId = "onboarding.co-day-5-skipped.displayed") }
            verify(exactly = 1) { telemetry.onStageCompleted(message) }
            assertNull(appStore.state.messaging.messageToShow[message.surface])
            binding.stop()
        }

    @Test
    fun `GIVEN a stage-completed message was displayed WHEN it is selected again THEN it is not recorded twice`() =
        runTest(testDispatcher) {
            val message = message("co-day-2-complete")
            present(message)

            appStore.dispatch(MessagingAction.UpdateMessageToShow(message))
            testDispatcher.scheduler.advanceUntilIdle()

            verify(exactly = 1) { events.recordEvent(count = 1, eventId = "onboarding.co-day-2-complete.displayed") }
            verify(exactly = 1) { telemetry.onStageCompleted(message) }
            binding.stop()
        }

    @Test
    fun `GIVEN the default browser role can be requested WHEN a system prompt message is selected THEN the role is requested`() =
        runTest(testDispatcher) {
            makeDefaultBrowserRoleRequestable()
            val message = message("co-default-browser-1")

            present(message)

            assertEquals(1, launcher.launched.size)
            assertTrue(MessagingAction.MessageDisplayed(message) in actions)
            verify { events.recordEvent(count = 1, eventId = "onboarding.co-default-browser-1.displayed") }
            binding.stop()
        }

    @Test
    fun `GIVEN the default browser role can't be requested WHEN a system prompt message is selected THEN it is dismissed`() =
        runTest(testDispatcher) {
            val message = message("co-default-browser-1")

            present(message)

            assertTrue(launcher.launched.isEmpty())
            assertTrue(MessagingAction.MessageDismissed(message) in actions)
            assertFalse(MessagingAction.MessageDisplayed(message) in actions)
            binding.stop()
        }

    @Test
    fun `GIVEN the role was requested WHEN it is granted THEN the message is clicked and the stage-completed messages are evaluated`() =
        runTest(testDispatcher) {
            makeDefaultBrowserRoleRequestable()
            val message = message("co-default-browser-1")
            present(message)
            actions.clear()

            binding.onSystemPromptResult(Activity.RESULT_OK)

            verify { telemetry.onDefaultBrowserRoleGranted() }
            assertTrue(MessagingAction.MessageClicked(message) in actions)
            assertEquals(
                listOf(FenixMessageSurfaceId.CONTINUOUS_ONBOARDING),
                evaluations().map { it.surface },
            )
            binding.stop()
        }

    @Test
    fun `GIVEN the role was requested WHEN it is not granted THEN the message is dismissed and the stage-completed messages are evaluated`() =
        runTest(testDispatcher) {
            makeDefaultBrowserRoleRequestable()
            val message = message("co-default-browser-1")
            present(message)
            actions.clear()

            binding.onSystemPromptResult(Activity.RESULT_CANCELED)
            testDispatcher.scheduler.advanceUntilIdle()

            verify(exactly = 0) { telemetry.onDefaultBrowserRoleGranted() }
            assertTrue(MessagingAction.MessageDismissed(message) in actions)
            verify { events.recordEventOrThrow(count = 1, eventId = "onboarding.co-default-browser-1.dismissed") }
            assertEquals(
                listOf(FenixMessageSurfaceId.CONTINUOUS_ONBOARDING),
                evaluations().map { it.surface },
            )
            binding.stop()
        }

    @Test
    fun `GIVEN no role request is pending WHEN a result arrives THEN nothing happens`() {
        binding.onSystemPromptResult(Activity.RESULT_OK)

        assertTrue(actions.isEmpty())
    }

    @Test
    fun `WHEN a card message is selected THEN the card is shown and the message is recorded as displayed`() =
        runTest(testDispatcher) {
            val message = syncCardMessage()

            present(message)

            assertNotNull(findCard())
            assertTrue(MessagingAction.MessageDisplayed(message) in actions)
            verify(exactly = 0) { messaging.getIntentForMessage(any()) }
            verify { events.recordEvent(count = 1, eventId = "onboarding.co-sync.displayed") }
            binding.stop()
        }

    @Test
    fun `GIVEN a card is shown WHEN a card message is selected THEN it is not recorded as displayed`() =
        runTest(testDispatcher) {
            present(notificationCardMessage("co-notifications-1"))
            val second = syncCardMessage()

            appStore.dispatch(MessagingAction.UpdateMessageToShow(second))
            testDispatcher.scheduler.advanceUntilIdle()

            assertFalse(MessagingAction.MessageDisplayed(second) in actions)
            assertFalse(MessagingAction.MessageDismissed(second) in actions)
            binding.stop()
        }

    @Test
    fun `GIVEN a message with an action continuous onboarding doesn't present WHEN it is selected THEN it is dismissed`() =
        runTest(testDispatcher) {
            val message = message("co-sync", buttonLabel = "Sync", action = "://settings")

            present(message)

            assertNull(findCard())
            assertTrue(MessagingAction.MessageDismissed(message) in actions)
            verify { events.recordEventOrThrow(count = 1, eventId = "onboarding.co-sync.dismissed") }
            binding.stop()
        }

    @Test
    fun `WHEN the notification card's primary button is clicked THEN the permission is requested directly and the click ends the card`() =
        runTest(testDispatcher) {
            val message =
                notificationCardMessage("co-notifications-1")

            cardPageState(message).primaryButton.onClick()
            testDispatcher.scheduler.advanceUntilIdle()

            verify { telemetry.onPrimaryClick(message) }
            verify(exactly = 1) { messaging.getIntentForMessage(message) }
            verify { notificationsDelegate.requestNotificationPermission() }
            assertNull(shadowOf(activity).nextStartedActivity)
            verify { events.recordEventOrThrow(count = 1, eventId = "onboarding.co-notifications-1.clicked") }
            assertFalse(actions.any { it is MessagingAction.MessageClicked || it is MessagingAction.MessageDismissed })
            assertEquals(
                listOf(FenixMessageSurfaceId.CONTINUOUS_ONBOARDING),
                evaluations().map { it.surface },
            )
        }

    @Test
    fun `WHEN the sync card's primary button is clicked THEN sign in is navigated to without dismissing the message`() =
        runTest(testDispatcher) {
            val message = syncCardMessage()

            cardPageState(message).primaryButton.onClick()
            testDispatcher.scheduler.advanceUntilIdle()

            assertEquals(1, syncSignInNavigations)
            assertNull(shadowOf(activity).nextStartedActivity)
            verify { events.recordEventOrThrow(count = 1, eventId = "onboarding.co-sync.clicked") }
            verify(exactly = 0) { events.recordEventOrThrow(any(), eventId = "onboarding.co-sync.dismissed") }
            assertFalse(actions.any { it is MessagingAction.MessageClicked || it is MessagingAction.MessageDismissed })
        }

    @Test
    fun `WHEN a card's secondary button is clicked THEN the message is dismissed and the stage-completed messages are evaluated`() =
        runTest(testDispatcher) {
            val message = notificationCardMessage("co-notifications-2")

            cardPageState(message).secondaryButton!!.onClick()
            testDispatcher.scheduler.advanceUntilIdle()

            verify { telemetry.onSecondaryClick(message) }
            assertTrue(MessagingAction.MessageDismissed(message) in actions)
            verify { events.recordEventOrThrow(count = 1, eventId = "onboarding.co-notifications-2.dismissed") }
            assertEquals(
                listOf(FenixMessageSurfaceId.CONTINUOUS_ONBOARDING),
                evaluations().map { it.surface },
            )
        }

    @Test
    fun `GIVEN an event can't be recorded WHEN a card is dismissed THEN the stage-completed messages are still evaluated`() =
        runTest(testDispatcher) {
            every { events.recordEventOrThrow(any(), any()) } returns
                CompletableDeferred<Unit>().apply {
                    completeExceptionally(NimbusException.DatabaseNotReady("not ready"))
                }
            val message = syncCardMessage()

            cardPageState(message).secondaryButton!!.onClick()
            testDispatcher.scheduler.advanceUntilIdle()

            assertEquals(
                listOf(FenixMessageSurfaceId.CONTINUOUS_ONBOARDING),
                evaluations().map { it.surface },
            )
        }

    @Test
    fun `GIVEN the user can be prompted WHEN the IP Protection message is selected THEN the prompt is shown once`() =
        runTest(testDispatcher) {
            val message = message("co-vpn")
            makeEligibleForIpProtection()

            present(message)
            ipProtectionPromptRepository.canShowIPProtectionPrompt = false
            actions.clear()
            appStore.dispatch(MessagingAction.UpdateMessageToShow(message))
            binding.onResume(TestLifecycleOwner())
            testDispatcher.scheduler.advanceUntilIdle()

            assertEquals(1, ipProtectionPrompts.size)
            verify(exactly = 1) { events.recordEvent(count = 1, eventId = "onboarding.co-vpn.displayed") }
            val context = evaluations().first().triggerContext?.get("continuous_onboarding") as Map<*, *>
            assertEquals(true, context["vpn_prompt_allowed"])
            binding.stop()
        }

    @Test
    fun `GIVEN the IP Protection account is not resolved WHEN the IP Protection message is selected THEN it waits`() =
        runTest(testDispatcher) {
            val message = message("co-vpn")
            ipProtectionStore.dispatch(
                IPProtectionAction.EligibilityAction.EligibilityChanged(EligibilityStatus.Eligible)
            )

            present(message)

            assertTrue(ipProtectionPrompts.isEmpty())
            assertFalse(MessagingAction.MessageDisplayed(message) in actions)

            ipProtectionStore.dispatch(IPProtectionAction.AccountAction.AccountStateChanged(AccountStatus.NoAccount))
            testDispatcher.scheduler.advanceUntilIdle()

            assertEquals(1, ipProtectionPrompts.size)
            assertTrue(MessagingAction.MessageDisplayed(message) in actions)
            binding.stop()
        }

    @Test
    fun `GIVEN the IP Protection message is waiting WHEN the user turns out ineligible THEN the stage is left to the stage-completed messages`() =
        runTest(testDispatcher) {
            val message = message("co-vpn")
            ipProtectionStore.dispatch(
                IPProtectionAction.EligibilityAction.EligibilityChanged(EligibilityStatus.Eligible)
            )
            present(message)
            actions.clear()

            ipProtectionStore.dispatch(
                IPProtectionAction.EligibilityAction.EligibilityChanged(EligibilityStatus.Ineligible)
            )
            testDispatcher.scheduler.advanceUntilIdle()

            assertTrue(ipProtectionPrompts.isEmpty())
            assertFalse(MessagingAction.MessageDisplayed(message) in actions)
            verify(exactly = 0) { events.recordEvent(any(), eventId = "onboarding.co-vpn.displayed") }
            assertTrue(FenixMessageSurfaceId.CONTINUOUS_ONBOARDING in evaluations().map { it.surface })
            binding.stop()
        }

    private fun evaluations() = actions.filterIsInstance<MessagingAction.Evaluate>()

    private fun present(message: Message) {
        binding.start()
        appStore.dispatch(MessagingAction.UpdateMessageToShow(message))
        testDispatcher.scheduler.advanceUntilIdle()
    }

    private fun cardPageState(message: Message) =
        binding.cardPageState(ContinuousOnboardingMessage.createFrom(message) as ContinuousOnboardingMessage.Card)

    private fun notificationCardMessage(id: String) = message(id, buttonLabel = "Turn on")

    private fun syncCardMessage() = message("co-sync", buttonLabel = "Sync")

    private fun findCard(): ComposeView? =
        (activity.window.decorView as ViewGroup).findViewWithTag(CONTINUOUS_ONBOARDING_DIALOG_TAG)

    @Suppress("DEPRECATION")
    private fun makeDefaultBrowserRoleRequestable() {
        shadowOf(activity.getSystemService(RoleManager::class.java)).addAvailableRole(RoleManager.ROLE_BROWSER)
    }

    private fun makeEligibleForIpProtection() {
        ipProtectionStore.dispatch(IPProtectionAction.EligibilityAction.EligibilityChanged(EligibilityStatus.Eligible))
        ipProtectionStore.dispatch(IPProtectionAction.AccountAction.AccountStateChanged(AccountStatus.NoAccount))
    }

    private fun message(id: String, buttonLabel: String? = null, action: String = actionFor(id)): Message {
        val data: MessageData = mockk(relaxed = true)
        every { data.surface } returns FenixMessageSurfaceId.CONTINUOUS_ONBOARDING
        every { data.buttonLabel } returns buttonLabel
        every { data.title } returns "title"
        every { data.text } returns "text"
        return Message(id, data, action, StyleData(), emptyList(), emptyList(), Message.Metadata(id))
    }

    private fun actionFor(id: String) =
        when {
            id.startsWith("co-default-browser") -> "://make_default_browser"
            id.startsWith("co-notifications") -> "://request_notification_permission"
            id.startsWith("co-sync") -> "://turn_on_sync"
            id.startsWith("co-vpn") -> "://settings_ip_protection"
            else -> "://continuous_onboarding_stage_completed"
        }

    private class FakeActivityResultLauncher : ActivityResultLauncher<Intent>() {
        val launched = mutableListOf<Intent>()

        override fun launch(input: Intent, options: ActivityOptionsCompat?) {
            launched += input
        }

        override fun unregister() = Unit

        override val contract: ActivityResultContract<Intent, *>
            get() = throw UnsupportedOperationException("Not used in tests.")
    }
}
