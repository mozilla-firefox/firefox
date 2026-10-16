/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.onboarding.continuous

import android.app.Activity
import android.content.Intent
import android.view.ViewGroup
import androidx.activity.result.ActivityResultLauncher
import androidx.activity.result.contract.ActivityResultContract
import androidx.appcompat.app.AppCompatActivity
import androidx.compose.ui.platform.ComposeView
import androidx.core.app.ActivityOptionsCompat
import androidx.test.filters.SdkSuppress
import kotlin.test.assertNotNull
import kotlinx.coroutines.test.StandardTestDispatcher
import kotlinx.coroutines.test.runTest
import mozilla.components.feature.ipprotection.store.IPProtectionAction
import mozilla.components.feature.ipprotection.store.IPProtectionStore
import mozilla.components.feature.ipprotection.store.state.AccountStatus
import mozilla.components.feature.ipprotection.store.state.EligibilityStatus
import mozilla.components.support.test.robolectric.testContext
import mozilla.components.support.utils.DateTimeProvider
import mozilla.components.support.utils.FakeDateTimeProvider
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith
import org.mozilla.fenix.GleanMetrics.Onboarding
import org.mozilla.fenix.R
import org.mozilla.fenix.ext.components
import org.mozilla.fenix.helpers.FenixGleanTestRule
import org.mozilla.fenix.ipprotection.FakeIPProtectionPromptRepository
import org.mozilla.fenix.onboarding.OnboardingReason
import org.mozilla.fenix.onboarding.OnboardingTelemetryRecorder
import org.mozilla.fenix.onboarding.view.Action
import org.mozilla.fenix.onboarding.view.OnboardingPageState
import org.mozilla.fenix.onboarding.view.OnboardingPageUiData
import org.mozilla.fenix.utils.Settings
import org.robolectric.Robolectric
import org.robolectric.RobolectricTestRunner

private const val CONTINUOUS_ONBOARDING_DIALOG_TAG = "continuous_onboarding_dialog"

@RunWith(RobolectricTestRunner::class)
class ContinuousOnboardingFeatureTest {
    @get:Rule val gleanTestRule = FenixGleanTestRule(testContext)

    private val testDispatcher = StandardTestDispatcher()

    private lateinit var activity: Activity
    private lateinit var settings: Settings
    private lateinit var telemetryRecorder: OnboardingTelemetryRecorder
    private lateinit var stageProvider: FakeContinuousOnboardingStageProvider
    private lateinit var dateTimeProvider: DateTimeProvider
    private lateinit var ipProtectionStore: IPProtectionStore
    private lateinit var ipProtectionPromptRepository: FakeIPProtectionPromptRepository
    private lateinit var feature: ContinuousOnboardingFeature

    @Before
    fun setup() {
        activity = Robolectric.buildActivity(AppCompatActivity::class.java).create().get()
        settings = Settings(testContext)
        telemetryRecorder =
            OnboardingTelemetryRecorder(
                onboardingReason = OnboardingReason.NEW_USER,
                installSource = "test",
            )
        dateTimeProvider = FakeDateTimeProvider()
        stageProvider = FakeContinuousOnboardingStageProvider()
        ipProtectionStore = IPProtectionStore()
        ipProtectionPromptRepository = FakeIPProtectionPromptRepository()
        feature =
            ContinuousOnboardingFeature(
                activity = activity,
                launcher = FakeActivityResultLauncher(),
                settings = settings,
                telemetryRecorder = telemetryRecorder,
                stageProvider = stageProvider,
                dateTimeProvider = dateTimeProvider,
                navigateToSyncSignIn = {},
                ipProtectionOnboardingConfig =
                    IPProtectionOnboardingConfig(
                        store = ipProtectionStore,
                        promptRepository = ipProtectionPromptRepository,
                        navigateToIpProtection = {},
                    ),
                ipProtectionMainDispatcher = testDispatcher,
            )
    }

    // start() gating

    @Test
    fun `WHEN feature disabled THEN start does not evaluate the stage`() {
        settings.continuousOnboardingFeatureEnabled = false
        settings.seventhDayOnboardingCompletedTimestamp = -1

        feature.start()

        assertEquals(0, stageProvider.callCount)
    }

    @Test
    fun `WHEN feature enabled and seventh day completed THEN start does not evaluate the stage`() {
        settings.continuousOnboardingFeatureEnabled = true
        settings.seventhDayOnboardingCompletedTimestamp = dateTimeProvider.currentTimeMillis()

        feature.start()

        assertEquals(0, stageProvider.callCount)
    }

    @Test
    fun `WHEN feature enabled and seventh day not completed THEN start evaluates the stage`() {
        settings.continuousOnboardingFeatureEnabled = true
        settings.seventhDayOnboardingCompletedTimestamp = -1

        feature.start()

        assertEquals(1, stageProvider.callCount)
    }

    // syncOnboardingPageState

    @Test
    fun `getSyncOnboardingPageState returns the expected state`() {
        val expectedState =
            OnboardingPageState(
                imageRes = R.drawable.nova_onboarding_sync,
                title = "Instantly pick up where you left off",
                description =
                    "Grab bookmarks, passwords, and more on any device in a snap. Your personal data stays safe and secure with encryption.",
                primaryButton =
                    Action(
                        text = "Start syncing",
                        onClick = {
                            telemetryRecorder.onSyncSignInClick(
                                sequenceId = OnboardingPageUiData.Type.SYNC_SIGN_IN.telemetryId,
                                sequencePosition = "0",
                            )
                        },
                    ),
                secondaryButton =
                    Action(
                        text = "Continue",
                        onClick = {
                            telemetryRecorder.onSkipSignInClick(
                                sequenceId = OnboardingPageUiData.Type.SYNC_SIGN_IN.telemetryId,
                                sequencePosition = "0",
                            )
                        },
                    ),
                onRecordImpressionEvent = {
                    telemetryRecorder.onImpression(
                        sequenceId = OnboardingPageUiData.Type.SYNC_SIGN_IN.telemetryId,
                        pageType = OnboardingPageUiData.Type.SYNC_SIGN_IN,
                        sequencePosition = "0",
                    )
                },
            )
        val actualState = feature.getSyncOnboardingPageState(ContinuousOnboardingStage.DAY_5)

        assertEquals(expectedState.imageRes, actualState.imageRes)
        assertEquals(expectedState.title, actualState.title)
        assertEquals(expectedState.description, actualState.description)
        assertEquals(expectedState.primaryButton.text, actualState.primaryButton.text)
        assertEquals(expectedState.secondaryButton?.text, actualState.secondaryButton?.text)
    }

    @Test
    fun `WHEN sync primary button is clicked THEN sign-in telemetry is recorded`() {
        val pageState = feature.getSyncOnboardingPageState(ContinuousOnboardingStage.DAY_5)

        pageState.primaryButton.onClick()

        assertNotNull(Onboarding.signIn.testGetValue())
        val signInEvent = Onboarding.signIn.testGetValue()!!.single()
        assertEquals("sync", signInEvent.extra!!["sequence_id"])
        assertEquals("0", signInEvent.extra!!["sequence_position"])

        assertNotNull(Onboarding.completed.testGetValue())
        val completedEvent = Onboarding.completed.testGetValue()!!.single()
        assertEquals("sync", completedEvent.extra!!["sequence_id"])
        assertEquals("0", completedEvent.extra!!["sequence_position"])

        assertNotNull(Onboarding.dismissed.testGetValue())
        assertEquals("completed", Onboarding.dismissed.testGetValue()!!.single().extra!!["method"])

        assertEquals(-1L, settings.fifthDayOnboardingCompletedTimestamp)
    }

    @Test
    fun `WHEN sync primary button is clicked THEN navigateToSyncSignIn is invoked`() {
        var navigateToSyncSignInInvoked = false
        val navigateToSyncSignIn = { navigateToSyncSignInInvoked = true }
        val feature =
            ContinuousOnboardingFeature(
                activity = activity,
                launcher = FakeActivityResultLauncher(),
                settings = settings,
                telemetryRecorder = telemetryRecorder,
                stageProvider = stageProvider,
                dateTimeProvider = dateTimeProvider,
                navigateToSyncSignIn = navigateToSyncSignIn,
                ipProtectionOnboardingConfig =
                    IPProtectionOnboardingConfig(
                        store = ipProtectionStore,
                        promptRepository = ipProtectionPromptRepository,
                        navigateToIpProtection = {},
                    ),
                ipProtectionMainDispatcher = testDispatcher,
            )
        val pageState = feature.getSyncOnboardingPageState(ContinuousOnboardingStage.DAY_5)

        pageState.primaryButton.onClick()

        assertTrue(navigateToSyncSignInInvoked)
    }

    @Test
    fun `WHEN sync secondary button is clicked THEN skip sign-in telemetry is recorded`() {
        val pageState = feature.getSyncOnboardingPageState(ContinuousOnboardingStage.DAY_5)

        pageState.secondaryButton!!.onClick()

        assertNotNull(Onboarding.skipSignIn.testGetValue())
        val skipEvent = Onboarding.skipSignIn.testGetValue()!!.single()
        assertEquals("sync", skipEvent.extra!!["sequence_id"])
        assertEquals("0", skipEvent.extra!!["sequence_position"])

        assertNotNull(Onboarding.completed.testGetValue())
        val completedEvent = Onboarding.completed.testGetValue()!!.single()
        assertEquals("sync", completedEvent.extra!!["sequence_id"])
        assertEquals("0", completedEvent.extra!!["sequence_position"])

        assertNotNull(Onboarding.dismissed.testGetValue())
        assertEquals("skipped", Onboarding.dismissed.testGetValue()!!.single().extra!!["method"])

        assertEquals(dateTimeProvider.currentTimeMillis(), settings.fifthDayOnboardingCompletedTimestamp)
    }

    @Test
    fun `WHEN sync impression event fires THEN sign-in card telemetry is recorded`() {
        val pageState = feature.getSyncOnboardingPageState(ContinuousOnboardingStage.DAY_5)

        pageState.onRecordImpressionEvent()

        assertNotNull(Onboarding.signInCard.testGetValue())
        val event = Onboarding.signInCard.testGetValue()!!.single()
        assertEquals("sync", event.extra!!["sequence_id"])
        assertEquals("0", event.extra!!["sequence_position"])
    }

    @Test
    fun `WHEN no sync button is clicked THEN no sign-in telemetry is recorded`() {
        feature.getSyncOnboardingPageState(ContinuousOnboardingStage.DAY_5)

        assertNull(Onboarding.signIn.testGetValue())
        assertNull(Onboarding.skipSignIn.testGetValue())
        assertNull(Onboarding.signInCard.testGetValue())
    }

    // notificationOnboardingPageState

    @SdkSuppress(minSdkVersion = 33)
    @Test
    fun `getNotificationOnboardingPageState returns the expected state`() {
        val expectedState =
            OnboardingPageState(
                imageRes = R.drawable.nova_onboarding_notifications,
                title = "Notifications help you stay safer with Firefox",
                description =
                    "Discover the latest privacy features in Firefox so you’re always up to date on how to stay protected.",
                primaryButton =
                    Action(
                        text = "Turn on notifications",
                        onClick = {
                            telemetryRecorder.onNotificationPermissionClick(
                                sequenceId = "notification",
                                sequencePosition = "0",
                            )
                            activity.components.notificationsDelegate.requestNotificationPermission()
                        },
                    ),
                secondaryButton =
                    Action(
                        text = "Not now",
                        onClick = {
                            telemetryRecorder.onSkipTurnOnNotificationsClick(
                                sequenceId = "notification",
                                sequencePosition = "0",
                            )
                        },
                    ),
                onRecordImpressionEvent = {
                    telemetryRecorder.onNotificationCardImpression(
                        sequenceId = "notification",
                        sequencePosition = "0",
                    )
                },
            )

        val actualState = feature.getNotificationOnboardingPageState(ContinuousOnboardingStage.DAY_2)

        assertEquals(expectedState.imageRes, actualState.imageRes)
        assertEquals(expectedState.title, actualState.title)
        assertEquals(expectedState.description, actualState.description)
        assertEquals(expectedState.primaryButton.text, actualState.primaryButton.text)
        assertEquals(expectedState.secondaryButton?.text, actualState.secondaryButton?.text)
    }

    @SdkSuppress(minSdkVersion = 33)
    @Test
    fun `WHEN notification primary button is clicked THEN turn on notification telemetry is recorded`() {
        val pageState = feature.getNotificationOnboardingPageState(ContinuousOnboardingStage.DAY_2)

        pageState.primaryButton.onClick()

        assertNotNull(Onboarding.turnOnNotifications.testGetValue())
        val event = Onboarding.turnOnNotifications.testGetValue()!!.single()
        assertEquals("notification", event.extra!!["sequence_id"])
        assertEquals("0", event.extra!!["sequence_position"])

        assertEquals(dateTimeProvider.currentTimeMillis(), settings.secondDayOnboardingCompletedTimestamp)
    }

    @SdkSuppress(minSdkVersion = 33)
    @Test
    fun `WHEN notification secondary button is clicked THEN skip notification telemetry is recorded`() {
        val pageState = feature.getNotificationOnboardingPageState(ContinuousOnboardingStage.DAY_2)

        pageState.secondaryButton!!.onClick()

        assertNotNull(Onboarding.skipTurnOnNotifications.testGetValue())
        val event = Onboarding.skipTurnOnNotifications.testGetValue()!!.single()
        assertEquals("notification", event.extra!!["sequence_id"])
        assertEquals("0", event.extra!!["sequence_position"])

        assertEquals(dateTimeProvider.currentTimeMillis(), settings.secondDayOnboardingCompletedTimestamp)
    }

    @SdkSuppress(minSdkVersion = 33)
    @Test
    fun `WHEN notification impression event fires THEN notification card telemetry is recorded`() {
        val pageState = feature.getNotificationOnboardingPageState(ContinuousOnboardingStage.DAY_2)

        pageState.onRecordImpressionEvent()

        assertNotNull(Onboarding.turnOnNotificationsCard.testGetValue())
        val event = Onboarding.turnOnNotificationsCard.testGetValue()!!.single()
        assertEquals("notification", event.extra!!["sequence_id"])
        assertEquals("0", event.extra!!["sequence_position"])
    }

    @SdkSuppress(minSdkVersion = 33)
    @Test
    fun `WHEN no notification button is clicked THEN no notification telemetry is recorded`() {
        feature.getNotificationOnboardingPageState(ContinuousOnboardingStage.DAY_2)

        assertNull(Onboarding.turnOnNotifications.testGetValue())
        assertNull(Onboarding.skipTurnOnNotifications.testGetValue())
        assertNull(Onboarding.turnOnNotificationsCard.testGetValue())
    }

    // IP Protection integration

    @Test
    fun `WHEN stage is DAY_7 AND eligibility and account become ready AND repository allows THEN navigateToIpProtection is invoked and seventh day timestamp is saved`() =
        runTest(testDispatcher) {
            settings.continuousOnboardingFeatureEnabled = true
            var navigateToIpProtectionInvoked = false
            val feature =
                ContinuousOnboardingFeature(
                    activity = activity,
                    launcher = FakeActivityResultLauncher(),
                    settings = settings,
                    telemetryRecorder = telemetryRecorder,
                    stageProvider = FakeContinuousOnboardingStageProvider(ContinuousOnboardingStage.DAY_7),
                    dateTimeProvider = dateTimeProvider,
                    navigateToSyncSignIn = {},
                    ipProtectionOnboardingConfig =
                        IPProtectionOnboardingConfig(
                            store = ipProtectionStore,
                            promptRepository = ipProtectionPromptRepository,
                            navigateToIpProtection = { navigateToIpProtectionInvoked = true },
                        ),
                    ipProtectionMainDispatcher = testDispatcher,
                )

            feature.start()
            ipProtectionStore.dispatch(IPProtectionAction.EligibilityChanged(EligibilityStatus.Eligible))
            ipProtectionStore.dispatch(IPProtectionAction.AccountStateChanged(AccountStatus.NoAccount))
            testDispatcher.scheduler.advanceUntilIdle()

            assertTrue(navigateToIpProtectionInvoked)
            assertEquals(dateTimeProvider.currentTimeMillis(), settings.seventhDayOnboardingCompletedTimestamp)
        }

    @Test
    fun `WHEN stage is DAY_7 AND repository does not allow the prompt THEN navigateToIpProtection is not invoked and seventh day timestamp is saved`() =
        runTest(testDispatcher) {
            settings.continuousOnboardingFeatureEnabled = true
            var navigateToIpProtectionInvoked = false
            val feature =
                ContinuousOnboardingFeature(
                    activity = activity,
                    launcher = FakeActivityResultLauncher(),
                    settings = settings,
                    telemetryRecorder = telemetryRecorder,
                    stageProvider = FakeContinuousOnboardingStageProvider(ContinuousOnboardingStage.DAY_7),
                    dateTimeProvider = dateTimeProvider,
                    navigateToSyncSignIn = {},
                    ipProtectionOnboardingConfig =
                        IPProtectionOnboardingConfig(
                            store = ipProtectionStore,
                            promptRepository = FakeIPProtectionPromptRepository(canShowIPProtectionPrompt = false),
                            navigateToIpProtection = { navigateToIpProtectionInvoked = true },
                        ),
                    ipProtectionMainDispatcher = testDispatcher,
                )

            feature.start()
            ipProtectionStore.dispatch(IPProtectionAction.EligibilityChanged(EligibilityStatus.Eligible))
            ipProtectionStore.dispatch(IPProtectionAction.AccountStateChanged(AccountStatus.NoAccount))
            testDispatcher.scheduler.advanceUntilIdle()

            assertFalse(navigateToIpProtectionInvoked)
            assertEquals(dateTimeProvider.currentTimeMillis(), settings.seventhDayOnboardingCompletedTimestamp)
            val completedEvent = Onboarding.completed.testGetValue()!!.single()
            assertEquals("ip_protection", completedEvent.extra!!["sequence_id"])
            assertEquals("completed", Onboarding.dismissed.testGetValue()!!.single().extra!!["method"])
        }

    @Test
    fun `GIVEN day seven was completed by showing the prompt WHEN the repository stops allowing it and the store emits again THEN the prompt is not shown a second time`() =
        runTest(testDispatcher) {
            settings.continuousOnboardingFeatureEnabled = true
            var navigateToIpProtectionCount = 0
            val feature =
                ContinuousOnboardingFeature(
                    activity = activity,
                    launcher = FakeActivityResultLauncher(),
                    settings = settings,
                    telemetryRecorder = telemetryRecorder,
                    stageProvider = FakeContinuousOnboardingStageProvider(ContinuousOnboardingStage.DAY_7),
                    dateTimeProvider = dateTimeProvider,
                    navigateToSyncSignIn = {},
                    ipProtectionOnboardingConfig =
                        IPProtectionOnboardingConfig(
                            store = ipProtectionStore,
                            promptRepository = ipProtectionPromptRepository,
                            navigateToIpProtection = { navigateToIpProtectionCount++ },
                        ),
                    ipProtectionMainDispatcher = testDispatcher,
                )

            feature.start()
            ipProtectionStore.dispatch(IPProtectionAction.EligibilityChanged(EligibilityStatus.Eligible))
            ipProtectionStore.dispatch(IPProtectionAction.AccountStateChanged(AccountStatus.NoAccount))
            testDispatcher.scheduler.advanceUntilIdle()

            ipProtectionPromptRepository.canShowIPProtectionPrompt = false
            ipProtectionStore.dispatch(IPProtectionAction.AccountStateChanged(AccountStatus.Authenticated))
            testDispatcher.scheduler.advanceUntilIdle()

            assertEquals(1, navigateToIpProtectionCount)
        }

    @Test
    fun `WHEN stage is DAY_7 AND user is not eligible for IP Protection THEN navigateToIpProtection is not invoked and seventh day timestamp is saved`() =
        runTest(testDispatcher) {
            listOf(EligibilityStatus.Ineligible, EligibilityStatus.UnsupportedRegion).forEach { ineligibleStatus ->
                settings.continuousOnboardingFeatureEnabled = true
                settings.seventhDayOnboardingCompletedTimestamp = -1L
                var navigateToIpProtectionInvoked = false
                val store = IPProtectionStore()
                val feature =
                    ContinuousOnboardingFeature(
                        activity = activity,
                        launcher = FakeActivityResultLauncher(),
                        settings = settings,
                        telemetryRecorder = telemetryRecorder,
                        stageProvider = FakeContinuousOnboardingStageProvider(ContinuousOnboardingStage.DAY_7),
                        dateTimeProvider = dateTimeProvider,
                        navigateToSyncSignIn = {},
                        ipProtectionOnboardingConfig =
                            IPProtectionOnboardingConfig(
                                store = store,
                                promptRepository = ipProtectionPromptRepository,
                                navigateToIpProtection = { navigateToIpProtectionInvoked = true },
                            ),
                        ipProtectionMainDispatcher = testDispatcher,
                    )

                feature.start()
                store.dispatch(IPProtectionAction.EligibilityChanged(ineligibleStatus))
                testDispatcher.scheduler.advanceUntilIdle()

                assertFalse(navigateToIpProtectionInvoked)
                assertEquals(dateTimeProvider.currentTimeMillis(), settings.seventhDayOnboardingCompletedTimestamp)
                val completedEvent = Onboarding.completed.testGetValue()!!.last()
                assertEquals("ip_protection", completedEvent.extra!!["sequence_id"])
                assertEquals("skipped", Onboarding.dismissed.testGetValue()!!.last().extra!!["method"])
            }
        }

    @Test
    fun `WHEN feature is stopped THEN the IP Protection binding stops observing eligibility`() =
        runTest(testDispatcher) {
            settings.continuousOnboardingFeatureEnabled = true
            var navigateToIpProtectionInvoked = false
            val feature =
                ContinuousOnboardingFeature(
                    activity = activity,
                    launcher = FakeActivityResultLauncher(),
                    settings = settings,
                    telemetryRecorder = telemetryRecorder,
                    stageProvider = FakeContinuousOnboardingStageProvider(ContinuousOnboardingStage.DAY_7),
                    dateTimeProvider = dateTimeProvider,
                    navigateToSyncSignIn = {},
                    ipProtectionOnboardingConfig =
                        IPProtectionOnboardingConfig(
                            store = ipProtectionStore,
                            promptRepository = ipProtectionPromptRepository,
                            navigateToIpProtection = { navigateToIpProtectionInvoked = true },
                        ),
                    ipProtectionMainDispatcher = testDispatcher,
                )

            feature.start()
            feature.stop()
            ipProtectionStore.dispatch(IPProtectionAction.EligibilityChanged(EligibilityStatus.Eligible))
            ipProtectionStore.dispatch(IPProtectionAction.AccountStateChanged(AccountStatus.NoAccount))
            testDispatcher.scheduler.advanceUntilIdle()

            assertFalse(navigateToIpProtectionInvoked)
        }

    @Test
    fun `WHEN feature is stopped and started again THEN the IP Protection binding still observes eligibility`() =
        runTest(testDispatcher) {
            settings.continuousOnboardingFeatureEnabled = true
            var navigateToIpProtectionInvoked = false
            val feature =
                ContinuousOnboardingFeature(
                    activity = activity,
                    launcher = FakeActivityResultLauncher(),
                    settings = settings,
                    telemetryRecorder = telemetryRecorder,
                    stageProvider = FakeContinuousOnboardingStageProvider(ContinuousOnboardingStage.DAY_7),
                    dateTimeProvider = dateTimeProvider,
                    navigateToSyncSignIn = {},
                    ipProtectionOnboardingConfig =
                        IPProtectionOnboardingConfig(
                            store = ipProtectionStore,
                            promptRepository = ipProtectionPromptRepository,
                            navigateToIpProtection = { navigateToIpProtectionInvoked = true },
                        ),
                    ipProtectionMainDispatcher = testDispatcher,
                )

            // Simulates the fragment view going through a stop/start cycle (e.g. backgrounding
            // the app) before eligibility changes, reusing the same IP Protection binding.
            feature.start()
            feature.stop()
            feature.start()
            ipProtectionStore.dispatch(IPProtectionAction.EligibilityChanged(EligibilityStatus.Eligible))
            ipProtectionStore.dispatch(IPProtectionAction.AccountStateChanged(AccountStatus.NoAccount))
            testDispatcher.scheduler.advanceUntilIdle()

            assertTrue(navigateToIpProtectionInvoked)
            assertEquals(dateTimeProvider.currentTimeMillis(), settings.seventhDayOnboardingCompletedTimestamp)
        }

    @Test
    fun `WHEN feature is stopped without ever starting THEN no exception is thrown`() {
        val feature =
            ContinuousOnboardingFeature(
                activity = activity,
                launcher = FakeActivityResultLauncher(),
                settings = settings,
                telemetryRecorder = telemetryRecorder,
                stageProvider = stageProvider,
                dateTimeProvider = dateTimeProvider,
                navigateToSyncSignIn = {},
                ipProtectionOnboardingConfig =
                    IPProtectionOnboardingConfig(
                        store = ipProtectionStore,
                        promptRepository = ipProtectionPromptRepository,
                        navigateToIpProtection = {},
                    ),
                ipProtectionMainDispatcher = testDispatcher,
            )

        feature.stop()
    }

    @Test
    fun `WHEN stage is DAY_7 AND feature is disabled THEN navigateToIpProtection is not invoked`() =
        runTest(testDispatcher) {
            settings.continuousOnboardingFeatureEnabled = false
            var navigateToIpProtectionInvoked = false
            val feature =
                ContinuousOnboardingFeature(
                    activity = activity,
                    launcher = FakeActivityResultLauncher(),
                    settings = settings,
                    telemetryRecorder = telemetryRecorder,
                    stageProvider = FakeContinuousOnboardingStageProvider(ContinuousOnboardingStage.DAY_7),
                    dateTimeProvider = dateTimeProvider,
                    navigateToSyncSignIn = {},
                    ipProtectionOnboardingConfig =
                        IPProtectionOnboardingConfig(
                            store = ipProtectionStore,
                            promptRepository = ipProtectionPromptRepository,
                            navigateToIpProtection = { navigateToIpProtectionInvoked = true },
                        ),
                    ipProtectionMainDispatcher = testDispatcher,
                )

            feature.start()
            ipProtectionStore.dispatch(IPProtectionAction.EligibilityChanged(EligibilityStatus.Eligible))
            ipProtectionStore.dispatch(IPProtectionAction.AccountStateChanged(AccountStatus.NoAccount))
            testDispatcher.scheduler.advanceUntilIdle()

            assertFalse(navigateToIpProtectionInvoked)
        }

    // markStageCompleted

    @Test
    fun `WHEN DAY_2 stage is completed THEN second day timestamp is saved`() {
        feature.markStageCompleted(ContinuousOnboardingStage.DAY_2)

        assertEquals(dateTimeProvider.currentTimeMillis(), settings.secondDayOnboardingCompletedTimestamp)
        assertEquals(-1L, settings.thirdDayOnboardingCompletedTimestamp)
        assertEquals(-1L, settings.fifthDayOnboardingCompletedTimestamp)
        assertEquals(-1L, settings.seventhDayOnboardingCompletedTimestamp)
    }

    @Test
    fun `WHEN DAY_3 stage is completed THEN third day timestamp is saved`() {
        feature.markStageCompleted(ContinuousOnboardingStage.DAY_3)

        assertEquals(-1L, settings.secondDayOnboardingCompletedTimestamp)
        assertEquals(dateTimeProvider.currentTimeMillis(), settings.thirdDayOnboardingCompletedTimestamp)
        assertEquals(-1L, settings.fifthDayOnboardingCompletedTimestamp)
        assertEquals(-1L, settings.seventhDayOnboardingCompletedTimestamp)
    }

    @Test
    fun `WHEN DAY_5 stage is completed THEN fifth day timestamp is saved`() {
        feature.markStageCompleted(ContinuousOnboardingStage.DAY_5)

        assertEquals(-1L, settings.secondDayOnboardingCompletedTimestamp)
        assertEquals(-1L, settings.thirdDayOnboardingCompletedTimestamp)
        assertEquals(dateTimeProvider.currentTimeMillis(), settings.fifthDayOnboardingCompletedTimestamp)
        assertEquals(-1L, settings.seventhDayOnboardingCompletedTimestamp)
    }

    @Test
    fun `WHEN DAY_7 stage is completed THEN seventh day timestamp is saved`() {
        feature.markStageCompleted(ContinuousOnboardingStage.DAY_7)

        assertEquals(-1L, settings.secondDayOnboardingCompletedTimestamp)
        assertEquals(-1L, settings.thirdDayOnboardingCompletedTimestamp)
        assertEquals(-1L, settings.fifthDayOnboardingCompletedTimestamp)
        assertEquals(dateTimeProvider.currentTimeMillis(), settings.seventhDayOnboardingCompletedTimestamp)
    }

    @Test
    fun `WHEN NONE stage is completed THEN no timestamps are changed`() {
        feature.markStageCompleted(ContinuousOnboardingStage.NONE)

        assertEquals(-1L, settings.secondDayOnboardingCompletedTimestamp)
        assertEquals(-1L, settings.thirdDayOnboardingCompletedTimestamp)
        assertEquals(-1L, settings.fifthDayOnboardingCompletedTimestamp)
        assertEquals(-1L, settings.seventhDayOnboardingCompletedTimestamp)
    }

    // onDefaultBrowserStepCompleted

    @Test
    fun `WHEN default browser step completed with RESULT_OK THEN set-to-default telemetry is recorded`() {
        feature.onDefaultBrowserStepCompleted(Activity.RESULT_OK)

        assertNotNull(Onboarding.setToDefault.testGetValue())
        val event = Onboarding.setToDefault.testGetValue()!!.single()
        assertEquals("default", event.extra!!["sequence_id"])
        assertEquals("0", event.extra!!["sequence_position"])
    }

    @Test
    fun `WHEN default browser step completed without RESULT_OK THEN no set-to-default telemetry is recorded`() {
        feature.onDefaultBrowserStepCompleted(Activity.RESULT_CANCELED)

        assertNull(Onboarding.setToDefault.testGetValue())
    }

    @Test
    fun `WHEN default browser step completed without RESULT_OK THEN pending stage is reset`() {
        feature.pendingStage = ContinuousOnboardingStage.DAY_2

        feature.onDefaultBrowserStepCompleted(Activity.RESULT_CANCELED)

        assertEquals(ContinuousOnboardingStage.NONE, feature.pendingStage)
    }

    @Test
    fun `WHEN default browser step completed with RESULT_OK THEN pending stage is reset`() {
        feature.pendingStage = ContinuousOnboardingStage.DAY_2

        feature.onDefaultBrowserStepCompleted(Activity.RESULT_OK)

        assertEquals(ContinuousOnboardingStage.NONE, feature.pendingStage)
    }

    // start() re-entrancy guard

    @Test
    fun `WHEN pendingStage is not none THEN start does not evaluate the stage`() {
        settings.continuousOnboardingFeatureEnabled = true
        val fakeStageProvider = FakeContinuousOnboardingStageProvider(ContinuousOnboardingStage.DAY_7)
        val feature =
            ContinuousOnboardingFeature(
                activity = activity,
                launcher = FakeActivityResultLauncher(),
                settings = settings,
                telemetryRecorder = telemetryRecorder,
                stageProvider = fakeStageProvider,
                dateTimeProvider = dateTimeProvider,
                navigateToSyncSignIn = {},
                ipProtectionOnboardingConfig =
                    IPProtectionOnboardingConfig(
                        store = ipProtectionStore,
                        promptRepository = ipProtectionPromptRepository,
                        navigateToIpProtection = {},
                    ),
                ipProtectionMainDispatcher = testDispatcher,
            )
        feature.pendingStage = ContinuousOnboardingStage.DAY_2

        feature.start()

        assertEquals(0, fakeStageProvider.callCount)
    }

    @Test
    fun `WHEN a continuous onboarding dialog is already showing THEN start does not evaluate the stage`() {
        settings.continuousOnboardingFeatureEnabled = true
        val fakeStageProvider = FakeContinuousOnboardingStageProvider(ContinuousOnboardingStage.DAY_7)
        val feature =
            ContinuousOnboardingFeature(
                activity = activity,
                launcher = FakeActivityResultLauncher(),
                settings = settings,
                telemetryRecorder = telemetryRecorder,
                stageProvider = fakeStageProvider,
                dateTimeProvider = dateTimeProvider,
                navigateToSyncSignIn = {},
                ipProtectionOnboardingConfig =
                    IPProtectionOnboardingConfig(
                        store = ipProtectionStore,
                        promptRepository = ipProtectionPromptRepository,
                        navigateToIpProtection = {},
                    ),
                ipProtectionMainDispatcher = testDispatcher,
            )
        val decorView = activity.window.decorView as ViewGroup
        decorView.addView(
            ComposeView(activity).apply {
                tag = CONTINUOUS_ONBOARDING_DIALOG_TAG
            }
        )

        feature.start()

        assertEquals(0, fakeStageProvider.callCount)
    }

    class FakeActivityResultLauncher : ActivityResultLauncher<Intent>() {
        override fun launch(input: Intent, options: ActivityOptionsCompat?) = Unit

        override fun unregister() = Unit

        override val contract: ActivityResultContract<Intent, *>
            get() = throw UnsupportedOperationException("Not used in tests.")
    }

    class FakeContinuousOnboardingStageProvider(
        private val stage: ContinuousOnboardingStage = ContinuousOnboardingStage.NONE
    ) : ContinuousOnboardingStageProvider {
        var callCount = 0
            private set

        override fun getContinuousOnboardingStage(): ContinuousOnboardingStage {
            callCount++
            return stage
        }
    }
}
