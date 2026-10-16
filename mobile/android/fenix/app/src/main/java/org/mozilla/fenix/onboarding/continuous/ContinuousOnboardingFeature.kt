/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.onboarding.continuous

import android.app.Activity
import android.app.role.RoleManager
import android.content.Intent
import android.os.Build
import android.view.ViewGroup
import androidx.activity.result.ActivityResultLauncher
import androidx.annotation.RequiresApi
import androidx.annotation.VisibleForTesting
import androidx.compose.ui.platform.ComposeView
import androidx.compose.ui.platform.ViewCompositionStrategy
import androidx.fragment.app.Fragment
import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.Dispatchers
import mozilla.components.feature.ipprotection.store.IPProtectionStore
import mozilla.components.support.base.feature.LifecycleAwareFeature
import mozilla.components.support.base.feature.ViewBoundFeatureWrapper
import mozilla.components.support.base.log.logger.Logger
import mozilla.components.support.utils.DateTimeProvider
import mozilla.components.support.utils.DefaultDateTimeProvider
import org.mozilla.fenix.R
import org.mozilla.fenix.ext.components
import org.mozilla.fenix.ipprotection.store.IPProtectionOnboardingPrompt
import org.mozilla.fenix.ipprotection.store.IPProtectionPromptRepository
import org.mozilla.fenix.onboarding.DismissedMethod
import org.mozilla.fenix.onboarding.OnboardingTelemetryRecorder
import org.mozilla.fenix.onboarding.OnboardingTelemetryRecorder.Companion.ET_CARD_CLOSE_BUTTON
import org.mozilla.fenix.onboarding.view.Action
import org.mozilla.fenix.onboarding.view.OnboardingPageState
import org.mozilla.fenix.onboarding.view.OnboardingPageUiData
import org.mozilla.fenix.theme.FirefoxTheme
import org.mozilla.fenix.utils.Settings

/**
 * Dependencies required to observe IP Protection eligibility and show its onboarding prompt.
 *
 * @property store The [IPProtectionStore] to observe for eligibility and account status changes.
 * @property promptRepository Source of truth for whether the onboarding prompt is still allowed to appear.
 * @property navigateToIpProtection Callback for when the IP Protection onboarding prompt should be shown to the user.
 */
class IPProtectionOnboardingConfig(
    val store: IPProtectionStore,
    val promptRepository: IPProtectionPromptRepository,
    val navigateToIpProtection: () -> Unit,
)

/**
 * Manages the continuous onboarding flow shown after initial onboarding.
 *
 * Based on the user's current onboarding stage and device capabilities, this feature may:
 * - on day 2 or day 3, request the default browser role, followed by a notification-permission onboarding card if
 *   available, skipping either step if already satisfied,
 * - on day 5, show a Firefox Sync sign-in card, or skip it if already signed in,
 * - on day 7, show the IP Protection onboarding prompt, or skip it if already satisfied or the user is not eligible for
 *   IP Protection.
 */
class ContinuousOnboardingFeature(
    private val activity: Activity,
    private val launcher: ActivityResultLauncher<Intent>,
    private val settings: Settings,
    private val telemetryRecorder: OnboardingTelemetryRecorder,
    private val navigateToSyncSignIn: () -> Unit,
    private val ipProtectionOnboardingConfig: IPProtectionOnboardingConfig,
    private val stageProvider: ContinuousOnboardingStageProvider = ContinuousOnboardingStageProviderDefault(settings),
    private val dateTimeProvider: DateTimeProvider = DefaultDateTimeProvider(),
    ipProtectionMainDispatcher: CoroutineDispatcher = Dispatchers.Main,
) : LifecycleAwareFeature {
    private val logger = Logger("ContinuousOnboardingFeature")

    @VisibleForTesting internal var pendingStage: ContinuousOnboardingStage = ContinuousOnboardingStage.NONE

    /**
     * Observes the IP Protection store, showing the IP Protection onboarding prompt once the user becomes eligible and
     * [IPProtectionPromptRepository] allows it.
     */
    private val ipProtectionBinding =
        IPProtectionOnboardingPrompt(
            repository = ipProtectionOnboardingConfig.promptRepository,
            mainDispatcher = ipProtectionMainDispatcher,
            store = ipProtectionOnboardingConfig.store,
            onShowOnboarding = {
                logger.info("Showing IP Protection onboarding prompt.")
                ipProtectionOnboardingConfig.navigateToIpProtection()
                completeDaySeven()
            },
            onIneligible = {
                logger.info("User is not eligible for IP Protection.")
                telemetryRecorder.onOnboardingComplete(
                    sequenceId = IP_PROTECTION_SEQUENCE_ID,
                    sequencePosition = "0",
                    dismissedMethod = DismissedMethod.SKIPPED,
                )
                completeDaySeven()
            },
            onAlreadySatisfied = {
                logger.info("IP Protection onboarding prompt is no longer allowed to be shown.")
                telemetryRecorder.onOnboardingComplete(
                    sequenceId = IP_PROTECTION_SEQUENCE_ID,
                    sequencePosition = "0",
                )
                completeDaySeven()
            },
        )

    /**
     * Marks the day-7 stage complete exactly once, then stops observing the IP Protection store so later state
     * emissions in the same session cannot complete the stage a second time.
     */
    private fun completeDaySeven() {
        if (settings.seventhDayOnboardingCompletedTimestamp != -1L) {
            logger.info("Day 7 stage already completed.")
            return
        }
        markStageCompleted(ContinuousOnboardingStage.DAY_7)
        ipProtectionBinding.stop()
    }

    override fun start() {
        val enabled = settings.continuousOnboardingFeatureEnabled
        logger.info("continuousOnboardingFeatureEnabled: $enabled")
        if (!enabled) return

        val completed = settings.continuousOnboardingCompleted
        logger.info("continuousOnboardingCompleted: $completed")
        if (completed) return

        if (isStageUIShowing()) return

        when (val stage = stageProvider.getContinuousOnboardingStage()) {
            ContinuousOnboardingStage.DAY_2,
            ContinuousOnboardingStage.DAY_3 -> maybeRequestDefaultBrowserRole(stage)

            ContinuousOnboardingStage.DAY_5 ->
                if (!settings.signedInFxaAccount) {
                    showSyncCardDialog()
                } else {
                    telemetryRecorder.onOnboardingComplete(
                        sequenceId = OnboardingPageUiData.Type.SYNC_SIGN_IN.telemetryId,
                        sequencePosition = "0",
                    )
                    markStageCompleted(stage)
                }

            ContinuousOnboardingStage.DAY_7 -> {
                logger.info("Observing IP Protection eligibility for day 7 onboarding.")
                ipProtectionBinding.start()
            }

            ContinuousOnboardingStage.NONE -> {
                logger.info("No continuous onboarding stage to show.")
            }
        }
    }

    override fun stop() {
        ipProtectionBinding.stop()
    }

    /**
     * Returns whether a continuous onboarding stage's UI is currently displayed.
     *
     * `pendingStage` tracks the period while the Android system role-request Activity is in progress, and
     * `isContinuousOnboardingDialogShowing()` tracks the follow-up onboarding dialog shown afterward. Together they
     * prevent the DAY_2/DAY_3 onboarding flow from being started again if `start()` is invoked multiple times.
     */
    private fun isStageUIShowing(): Boolean {
        if (pendingStage != ContinuousOnboardingStage.NONE || isContinuousOnboardingDialogShowing()) {
            logger.info("Continuous onboarding stage UI is already showing.")
            return true
        }
        return false
    }

    private fun isContinuousOnboardingDialogShowing(): Boolean {
        val decorView = activity.window.decorView as? ViewGroup ?: return false
        return decorView.findViewWithTag<ComposeView>(CONTINUOUS_ONBOARDING_DIALOG_TAG) != null
    }

    private fun maybeRequestDefaultBrowserRole(stage: ContinuousOnboardingStage) {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
            val roleManager = activity.getSystemService(RoleManager::class.java)
            if (roleManager == null) {
                logger.warn("Role manager was null.")
                markStageCompleted(stage)
                return
            }

            if (
                roleManager.isRoleAvailable(RoleManager.ROLE_BROWSER) &&
                    !roleManager.isRoleHeld(RoleManager.ROLE_BROWSER)
            ) {
                logger.info("Showing default-browser role request dialog.")
                pendingStage = stage
                launcher.launch(roleManager.createRequestRoleIntent(RoleManager.ROLE_BROWSER))
            } else {
                logger.info("Default-browser role is already held or is unavailable.")
                maybeShowNotificationCardDialog(stage)
            }
        } else {
            logger.warn("Unable to show default-browser role request dialog.")
            markStageCompleted(stage)
        }
    }

    private fun showSyncCardDialog() {
        logger.info("Showing sync card dialog.")

        val stage = ContinuousOnboardingStage.DAY_5
        val onCloseButtonClicked = {
            logger.info("Closed the sync card dialog.")
            markStageCompleted(stage)

            telemetryRecorder.onSkipSignInClick(
                sequenceId = OnboardingPageUiData.Type.SYNC_SIGN_IN.telemetryId,
                sequencePosition = "0",
                elementType = ET_CARD_CLOSE_BUTTON,
            )
            telemetryRecorder.onOnboardingComplete(
                sequenceId = OnboardingPageUiData.Type.SYNC_SIGN_IN.telemetryId,
                sequencePosition = "0",
                dismissedMethod = DismissedMethod.SKIPPED,
            )
        }

        showDialog(
            pageState = getSyncOnboardingPageState(stage),
            onCloseButtonClicked = onCloseButtonClicked,
        )
    }

    @VisibleForTesting
    internal fun getSyncOnboardingPageState(stage: ContinuousOnboardingStage) =
        OnboardingPageState(
            imageRes = R.drawable.nova_onboarding_sync,
            title = activity.getString(R.string.nova_onboarding_sync_title),
            description = activity.getString(R.string.nova_onboarding_sync_subtitle),
            primaryButton =
                Action(
                    text = activity.getString(R.string.nova_onboarding_sync_button),
                    onClick = {
                        logger.info("Sync card dialog primary button click.")
                        navigateToSyncSignIn()

                        telemetryRecorder.onSyncSignInClick(
                            sequenceId = OnboardingPageUiData.Type.SYNC_SIGN_IN.telemetryId,
                            sequencePosition = "0",
                        )
                        telemetryRecorder.onOnboardingComplete(
                            sequenceId = OnboardingPageUiData.Type.SYNC_SIGN_IN.telemetryId,
                            sequencePosition = "0",
                        )
                    },
                ),
            secondaryButton =
                Action(
                    text = activity.getString(R.string.nova_onboarding_continue_button),
                    onClick = {
                        logger.info("Sync card dialog secondary button click.")
                        markStageCompleted(stage)

                        telemetryRecorder.onSkipSignInClick(
                            sequenceId = OnboardingPageUiData.Type.SYNC_SIGN_IN.telemetryId,
                            sequencePosition = "0",
                        )
                        telemetryRecorder.onOnboardingComplete(
                            sequenceId = OnboardingPageUiData.Type.SYNC_SIGN_IN.telemetryId,
                            sequencePosition = "0",
                            dismissedMethod = DismissedMethod.SKIPPED,
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

    /**
     * Continues the onboarding flow after the default-browser role request has returned.
     *
     * Invoked with the result of the system role request. Depending on the result code and device capabilities, this
     * may show the notification-permission onboarding card or mark the stage as completed.
     *
     * @param resultCode The result code returned by the system role request.
     */
    fun onDefaultBrowserStepCompleted(resultCode: Int) {
        if (resultCode == Activity.RESULT_OK) {
            telemetryRecorder.onSetToDefaultClick(
                sequenceId = OnboardingPageUiData.Type.DEFAULT_BROWSER.telemetryId,
                sequencePosition = "0",
            )
        }

        maybeShowNotificationCardDialog(pendingStage)

        // Reset the pending stage
        pendingStage = ContinuousOnboardingStage.NONE
    }

    private fun maybeShowNotificationCardDialog(stage: ContinuousOnboardingStage) {
        if (
            Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU &&
                !activity.components.notificationsDelegate.hasPostNotificationsPermission()
        ) {
            logger.info("Showing notification-permission card dialog.")

            val onCloseButtonClicked = {
                logger.info("Closed the notification-permission card dialog.")
                markStageCompleted(stage)

                telemetryRecorder.onSkipTurnOnNotificationsClick(
                    sequenceId = NOTIFICATION_SEQUENCE_ID,
                    sequencePosition = "0",
                    elementType = ET_CARD_CLOSE_BUTTON,
                )
            }

            showDialog(
                pageState = getNotificationOnboardingPageState(stage),
                onCloseButtonClicked = onCloseButtonClicked,
            )
        } else {
            logger.warn("Unable to show notification-permission card dialog.")
            markStageCompleted(stage)
        }
    }

    @VisibleForTesting
    @RequiresApi(Build.VERSION_CODES.TIRAMISU)
    internal fun getNotificationOnboardingPageState(stage: ContinuousOnboardingStage) =
        OnboardingPageState(
            imageRes = R.drawable.nova_onboarding_notifications,
            title = activity.getString(R.string.nova_onboarding_notifications_title),
            description = activity.getString(R.string.nova_onboarding_notifications_subtitle),
            primaryButton =
                Action(
                    text = activity.getString(R.string.nova_onboarding_notifications_button),
                    onClick = {
                        logger.info("Notification card dialog primary button click.")
                        activity.components.notificationsDelegate.requestNotificationPermission()
                        markStageCompleted(stage)

                        telemetryRecorder.onNotificationPermissionClick(
                            sequenceId = NOTIFICATION_SEQUENCE_ID,
                            sequencePosition = "0",
                        )
                    },
                ),
            secondaryButton =
                Action(
                    text = activity.getString(R.string.nova_onboarding_negative_button),
                    onClick = {
                        logger.info("Notification card dialog secondary button click.")
                        markStageCompleted(stage)

                        telemetryRecorder.onSkipTurnOnNotificationsClick(
                            sequenceId = NOTIFICATION_SEQUENCE_ID,
                            sequencePosition = "0",
                        )
                    },
                ),
            onRecordImpressionEvent = {
                telemetryRecorder.onNotificationCardImpression(
                    sequenceId = NOTIFICATION_SEQUENCE_ID,
                    sequencePosition = "0",
                )
            },
        )

    private fun showDialog(
        pageState: OnboardingPageState,
        onCloseButtonClicked: () -> Unit = {},
    ) {
        val decorView = activity.window.decorView as? ViewGroup
        if (decorView == null) {
            logger.error("decorView was null.")
            return
        }

        if (decorView.findViewWithTag<ComposeView>(CONTINUOUS_ONBOARDING_DIALOG_TAG) != null) {
            logger.error("Continuous onboarding dialog is already shown.")
            return
        }

        val composeView =
            ComposeView(activity).apply {
                tag = CONTINUOUS_ONBOARDING_DIALOG_TAG
            }
        val removeDialogView = {
            logger.info("Removing continuous onboarding dialog.")
            decorView.removeView(composeView)
        }

        composeView.apply {
            setViewCompositionStrategy(ViewCompositionStrategy.DisposeOnDetachedFromWindow)
            setContent {
                FirefoxTheme {
                    ContinuousOnboardingScreen(
                        pageState = pageState,
                        onCloseButtonClicked = onCloseButtonClicked,
                        removeDialogView = removeDialogView,
                    )
                }
            }
        }

        decorView.addView(composeView)
    }

    @VisibleForTesting
    internal fun markStageCompleted(stage: ContinuousOnboardingStage) {
        logger.info("Marking stage as completed: $stage")
        val now = dateTimeProvider.currentTimeMillis()
        when (stage) {
            ContinuousOnboardingStage.DAY_2 -> settings.secondDayOnboardingCompletedTimestamp = now
            ContinuousOnboardingStage.DAY_3 -> settings.thirdDayOnboardingCompletedTimestamp = now
            ContinuousOnboardingStage.DAY_5 -> settings.fifthDayOnboardingCompletedTimestamp = now
            ContinuousOnboardingStage.DAY_7 -> settings.seventhDayOnboardingCompletedTimestamp = now
            ContinuousOnboardingStage.NONE -> Unit
        }
    }

    companion object {
        private const val CONTINUOUS_ONBOARDING_DIALOG_TAG = "continuous_onboarding_dialog"
        private const val NOTIFICATION_SEQUENCE_ID = "notification"
        private const val IP_PROTECTION_SEQUENCE_ID = "ip_protection"

        /**
         * Convenience method to register [ContinuousOnboardingFeature] with a [Fragment]. Upon destruction of the
         * fragment's view, the binding will be unregistered and all references cleared.
         *
         * @param fragment The [Fragment] to register with.
         * @param binding The [ViewBoundFeatureWrapper] to bind the feature to.
         * @param launcher The [ActivityResultLauncher] used to request system roles.
         * @param telemetryRecorder Used to record onboarding telemetry.
         * @param navigateToSyncSignIn Invoked when the user chooses to sign in to Firefox Sync.
         * @param navigateToIpProtection Invoked when the IP Protection onboarding prompt should be shown.
         */
        fun register(
            fragment: Fragment,
            binding: ViewBoundFeatureWrapper<ContinuousOnboardingFeature>,
            launcher: ActivityResultLauncher<Intent>,
            telemetryRecorder: OnboardingTelemetryRecorder,
            navigateToSyncSignIn: () -> Unit,
            navigateToIpProtection: () -> Unit,
        ) {
            val components = fragment.requireContext().components
            val settings = components.settings

            binding.set(
                feature =
                    ContinuousOnboardingFeature(
                        activity = fragment.requireActivity(),
                        launcher = launcher,
                        settings = settings,
                        telemetryRecorder = telemetryRecorder,
                        navigateToSyncSignIn = navigateToSyncSignIn,
                        ipProtectionOnboardingConfig =
                            IPProtectionOnboardingConfig(
                                store = components.ipProtection.store,
                                promptRepository = components.ipProtectionPromptRepository,
                                navigateToIpProtection = navigateToIpProtection,
                            ),
                    ),
                owner = fragment.viewLifecycleOwner,
                view = fragment.requireView(),
            )
        }
    }
}
