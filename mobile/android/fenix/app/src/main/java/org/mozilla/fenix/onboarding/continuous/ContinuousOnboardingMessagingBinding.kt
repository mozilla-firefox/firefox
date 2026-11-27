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
import androidx.annotation.VisibleForTesting
import androidx.compose.ui.platform.ComposeView
import androidx.compose.ui.platform.ViewCompositionStrategy
import androidx.fragment.app.Fragment
import androidx.lifecycle.LifecycleOwner
import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.cancelChildren
import kotlinx.coroutines.coroutineScope
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.distinctUntilChanged
import kotlinx.coroutines.flow.distinctUntilChangedBy
import kotlinx.coroutines.flow.filter
import kotlinx.coroutines.flow.filterNotNull
import kotlinx.coroutines.flow.map
import kotlinx.coroutines.launch
import mozilla.components.ExperimentalAndroidComponentsApi
import mozilla.components.concept.base.crash.CrashReporting
import mozilla.components.feature.ipprotection.store.IPProtectionStore
import mozilla.components.feature.ipprotection.store.state.IPProtectionState
import mozilla.components.lib.state.helpers.AbstractBinding
import mozilla.components.service.nimbus.messaging.Message
import mozilla.components.service.nimbus.messaging.NimbusMessagingControllerInterface
import mozilla.components.support.base.android.NotificationsDelegate
import mozilla.components.support.base.feature.ViewBoundFeatureWrapper
import mozilla.components.support.base.log.logger.Logger
import org.mozilla.experiments.nimbus.NimbusEventStore
import org.mozilla.experiments.nimbus.internal.NimbusException
import org.mozilla.fenix.components.AppStore
import org.mozilla.fenix.components.appstate.AppAction.MessagingAction
import org.mozilla.fenix.ext.components
import org.mozilla.fenix.ipprotection.store.IPProtectionOnboardingPrompt
import org.mozilla.fenix.ipprotection.store.IPProtectionPromptRepository
import org.mozilla.fenix.ipprotection.store.IPProtectionPromptState.PromptDetails
import org.mozilla.fenix.messaging.FenixMessageSurfaceId
import org.mozilla.fenix.onboarding.OnboardingTelemetryRecorder
import org.mozilla.fenix.onboarding.continuous.ContinuousOnboardingFeature.Companion.CONTINUOUS_ONBOARDING_DIALOG_TAG
import org.mozilla.fenix.onboarding.view.Action
import org.mozilla.fenix.onboarding.view.OnboardingPageState
import org.mozilla.fenix.theme.FirefoxTheme
import org.mozilla.fenix.utils.Settings


/**
 * Nimbus event recorded when the continuous onboarding message with the given [messageId] is displayed. Message
 * triggers use it to sequence the messages.
 */
fun continuousOnboardingDisplayedEventId(messageId: String) = "onboarding.$messageId.displayed"

/**
 * Nimbus event recorded when the primary button of the card for the continuous onboarding message with the given
 * [messageId] is clicked.
 */
fun continuousOnboardingClickedEventId(messageId: String) = "onboarding.$messageId.clicked"

/** Nimbus event recorded when the continuous onboarding message with the given [messageId] is dismissed. */
fun continuousOnboardingDismissedEventId(messageId: String) = "onboarding.$messageId.dismissed"

/**
 * Dependencies of [ContinuousOnboardingMessagingBinding] that come from the app's components.
 *
 * @property settings The [Settings] used to read continuous onboarding state.
 * @property appStore The [AppStore] holding the messages.
 * @property messaging The controller used to record clicks on the messages.
 * @property events The [NimbusEventStore] the messages' displays and dismissals are recorded in.
 * @property notificationsDelegate Checks the notification permission.
 * @property ipProtectionStore The [IPProtectionStore] to observe for eligibility.
 * @property ipProtectionPromptRepository Whether the IP Protection prompt is still allowed to appear.
 * @property crashReporter Reports invalid IP Protection prompt configuration.
 */
class ContinuousOnboardingMessagingDependencies(
    val settings: Settings,
    val appStore: AppStore,
    val messaging: NimbusMessagingControllerInterface,
    val events: NimbusEventStore,
    val notificationsDelegate: NotificationsDelegate,
    val ipProtectionStore: IPProtectionStore,
    val ipProtectionPromptRepository: IPProtectionPromptRepository,
    val crashReporter: CrashReporting,
)

/**
 * Presents the continuous onboarding messages, the Messaging System counterpart of [ContinuousOnboardingFeature].
 *
 * The messages are defined in `messaging-evergreen-messages.fml.yaml`, and when they are shown by their triggers in
 * `messaging-fenix.fml.yaml`.
 * This binding evaluates them on the [FenixMessageSurfaceId.CONTINUOUS_ONBOARDING] surface, which selects at most one
 * message at a time, and presents the message according to its [ContinuousOnboardingMessage]:
 * - [ContinuousOnboardingMessage.StageCompleted]: nothing, the message only records that a stage is over,
 * - [ContinuousOnboardingMessage.DefaultBrowserRoleRequest]: the default browser role request,
 * - [ContinuousOnboardingMessage.Card]: the notification and sync cards, using [ContinuousOnboardingScreen],
 * - [ContinuousOnboardingMessage.IpProtectionPrompt]: the IP Protection prompt, whose content comes from the IP
 *   Protection feature.
 *
 * It records each message's display, primary button click and dismissal in the Nimbus event store, see
 * [continuousOnboardingDisplayedEventId], so that the triggers can sequence the messages.
 *
 * @param activity The [Activity] the cards are shown in.
 * @param launcher The [ActivityResultLauncher] used to request system roles.
 * @param dependencies The [ContinuousOnboardingMessagingDependencies].
 * @param telemetry Records the onboarding telemetry for the messages.
 * @param navigateToSyncSignIn Invoked when the user chooses to sign in to Firefox Sync.
 * @param navigateToIpProtection Invoked with the IP Protection onboarding prompt details.
 * @param mainDispatcher Dispatcher on which the stores are observed.
 */
@Suppress("LongParameterList")
class ContinuousOnboardingMessagingBinding(
    private val activity: Activity,
    private val launcher: ActivityResultLauncher<Intent>,
    private val dependencies: ContinuousOnboardingMessagingDependencies,
    private val telemetry: ContinuousOnboardingMessageTelemetry,
    private val navigateToSyncSignIn: () -> Unit,
    private val navigateToIpProtection: (PromptDetails) -> Unit,
    mainDispatcher: CoroutineDispatcher = Dispatchers.Main,
) : AbstractBinding<IPProtectionState>(dependencies.ipProtectionStore, mainDispatcher) {
    private val logger = Logger("ContinuousOnboardingMessagingBinding")
    private val appStore = dependencies.appStore
    private val scope = CoroutineScope(mainDispatcher)

    private var pendingSystemPrompt: Message? = null
    private var ipProtectionPrompt: IPProtectionOnboardingPrompt? = null
    private var ipProtectionPromptShown = false

    // Displays are recorded asynchronously, so a stage-completed message can be selected again before its triggers see
    // the record.
    private val displayedStageCompletedMessages = mutableSetOf<String>()

    override fun start() {
        // Messages are only restored at startup, which may have been before this install started continuous onboarding.
        if (appStore.state.messaging.messages.isEmpty()) {
            appStore.dispatch(MessagingAction.Restore)
        }
        super.start()
    }

    override fun stop() {
        super.stop()
        scope.coroutineContext.cancelChildren()
        stopIpProtectionPrompt()
    }

    override suspend fun onState(flow: Flow<IPProtectionState>) {
        coroutineScope {
            launch { observeIpProtectionEligibility(flow) }
            launch { observeMessagesRestored() }
            launch { observeMessages() }
        }
    }

    /** Evaluates the IP Protection stage when IP Protection eligibility, an input to its trigger context, changes. */
    @OptIn(ExperimentalAndroidComponentsApi::class)
    private suspend fun observeIpProtectionEligibility(flow: Flow<IPProtectionState>) {
        flow
            .map { Triple(it.eligibilityStatus, it.serviceStatus, it.currentAuthProvider) }
            .distinctUntilChanged()
            .collect { evaluate() }
    }

    /**
     * Evaluates the messages once they have been restored, which happens asynchronously at startup, so the first resume
     * after a cold start doesn't miss a stage.
     */
    private suspend fun observeMessagesRestored() {
        appStore.stateFlow
            .map { it.messaging.messages.isNotEmpty() }
            .distinctUntilChanged()
            .filter { it }
            .collect { evaluate() }
    }

    /**
     * Presents each selected message once, consuming it as soon as it is presented so that recreating the view does not
     * present it again.
     */
    private suspend fun observeMessages() {
        appStore.stateFlow
            .map { it.messaging.messageToShow[FenixMessageSurfaceId.CONTINUOUS_ONBOARDING] }
            .distinctUntilChangedBy { it?.id }
            .filterNotNull()
            .collect { message ->
                appStore.dispatch(MessagingAction.ConsumeMessageToShow(FenixMessageSurfaceId.CONTINUOUS_ONBOARDING))
                onMessage(message)
            }
    }

    private fun onMessage(message: Message) {
        when (val onboardingMessage = ContinuousOnboardingMessage.createFrom(message)) {
            is ContinuousOnboardingMessage.StageCompleted -> onStageCompletedMessage(message)
            is ContinuousOnboardingMessage.DefaultBrowserRoleRequest -> onDefaultBrowserRoleRequestMessage(message)
            is ContinuousOnboardingMessage.Card -> onCardMessage(onboardingMessage)
            is ContinuousOnboardingMessage.IpProtectionPrompt -> onIpProtectionMessage(message)
            null -> {
                logger.warn("Message ${message.id} can't be presented by continuous onboarding.")
                dismiss(message)
            }
        }
    }

    override fun onResume(owner: LifecycleOwner) {
        evaluate()
    }

    /**
     * Completes the default browser role request.
     *
     * @param resultCode The result code returned by the system role request.
     */
    fun onSystemPromptResult(resultCode: Int) {
        val message = pendingSystemPrompt ?: return
        pendingSystemPrompt = null
        if (resultCode == Activity.RESULT_OK) {
            telemetry.onDefaultBrowserRoleGranted()
            appStore.dispatch(MessagingAction.MessageClicked(message))
            evaluate()
        } else {
            dismiss(message)
        }
    }

    private fun evaluate() {
        appStore.dispatch(MessagingAction.Evaluate(FenixMessageSurfaceId.CONTINUOUS_ONBOARDING, triggerContext()))
    }

    private fun triggerContext(): Map<String, Any?> =
        dependencies.ipProtectionStore.state.toContinuousOnboardingMessageContext(
            enabled = dependencies.settings.continuousOnboardingFeatureEnabled,
            defaultBrowserRoleRequestable = isDefaultBrowserRoleRequestable(),
            notificationPermissionRequestable = isNotificationPermissionRequestable(),
            // Showing the prompt stops it from being allowed again, which must not read as "already satisfied".
            ipProtectionPromptAllowed =
                ipProtectionPromptShown || dependencies.ipProtectionPromptRepository.canShowIPProtectionPrompt(),
        )

    private fun isDefaultBrowserRoleRequestable(): Boolean {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.Q) return false
        val roleManager = activity.getSystemService(RoleManager::class.java) ?: return false
        return roleManager.isRoleAvailable(RoleManager.ROLE_BROWSER) && !roleManager.isRoleHeld(RoleManager.ROLE_BROWSER)
    }

    private fun isNotificationPermissionRequestable(): Boolean =
        Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU &&
            !dependencies.notificationsDelegate.hasPostNotificationsPermission()

    private fun displayed(message: Message) {
        appStore.dispatch(MessagingAction.MessageDisplayed(message))
        dependencies.events.recordEvent(count = 1, eventId = continuousOnboardingDisplayedEventId(message.id))
    }

    private fun dismiss(message: Message) {
        appStore.dispatch(MessagingAction.MessageDismissed(message))
        recordThenEvaluate(continuousOnboardingDismissedEventId(message.id))
    }

    /** Records an event that can make another message due, and evaluates once the triggers can see it. */
    private fun recordThenEvaluate(eventId: String) {
        scope.launch {
            try {
                dependencies.events.recordEventOrThrow(count = 1, eventId = eventId).await()
            } catch (e: NimbusException) {
                logger.error("Unable to record $eventId", e)
            }
            evaluate()
        }
    }

    private fun onStageCompletedMessage(message: Message) {
        if (!displayedStageCompletedMessages.add(message.id)) return
        displayed(message)
        telemetry.onStageCompleted(message)
    }

    private fun onDefaultBrowserRoleRequestMessage(message: Message) {
        if (pendingSystemPrompt != null) return

        if (!isDefaultBrowserRoleRequestable()) {
            logger.warn("Unable to launch the default browser role request for ${message.id}.")
            dismiss(message)
            return
        }

        val roleManager = activity.getSystemService(RoleManager::class.java)
        pendingSystemPrompt = message
        launcher.launch(roleManager.createRequestRoleIntent(RoleManager.ROLE_BROWSER))
        displayed(message)
    }

    private fun onCardMessage(card: ContinuousOnboardingMessage.Card) {
        val message = card.original
        val isShown =
            showDialog(
                pageState = cardPageState(card),
                onCloseButtonClicked = {
                    telemetry.onCloseClick(message)
                    dismiss(message)
                },
            )
        if (isShown) {
            displayed(message)
        }
    }

    @VisibleForTesting
    internal fun cardPageState(card: ContinuousOnboardingMessage.Card) =
        OnboardingPageState(
            imageRes = card.type.imageRes,
            title = card.title,
            description = card.text,
            primaryButton =
                Action(
                    text = card.primaryButtonLabel,
                    onClick = {
                        telemetry.onPrimaryClick(card.original)
                        // Records the Messaging System's click telemetry. The intent is not used: see performAction.
                        dependencies.messaging.getIntentForMessage(card.original)
                        performAction(card.type)
                        recordThenEvaluate(continuousOnboardingClickedEventId(card.original.id))
                    },
                ),
            secondaryButton =
                Action(
                    text = activity.getString(card.type.secondaryButtonLabel),
                    onClick = {
                        telemetry.onSecondaryClick(card.original)
                        dismiss(card.original)
                    },
                ),
            onRecordImpressionEvent = { telemetry.onImpression(card.original) },
        )

    /**
     * Performs the card's action. The message is not marked as clicked, so that the triggers decide whether the card
     * comes back: the sync card does until it is dismissed, as in [ContinuousOnboardingFeature].
     */
    private fun performAction(type: ContinuousOnboardingMessage.Card.Type) {
        when (type) {
            // Requested directly, as in ContinuousOnboardingFeature, rather than by relaunching the activity.
            ContinuousOnboardingMessage.Card.Type.REQUEST_NOTIFICATION_PERMISSION ->
                dependencies.notificationsDelegate.requestNotificationPermission()
            // Navigated directly, rather than through the deep link, to keep the onboarding FxA entry point.
            ContinuousOnboardingMessage.Card.Type.TURN_ON_SYNC -> navigateToSyncSignIn()
        }
    }

    private fun onIpProtectionMessage(message: Message) {
        if (ipProtectionPrompt != null || ipProtectionPromptShown) return

        ipProtectionPrompt =
            IPProtectionOnboardingPrompt(
                    repository = dependencies.ipProtectionPromptRepository,
                    crashReporter = dependencies.crashReporter,
                    mainDispatcher = mainDispatcher,
                    store = dependencies.ipProtectionStore,
                    onShowOnboarding = { promptDetails ->
                        logger.info("Showing IP Protection onboarding prompt for ${message.id}: $promptDetails")
                        // Invalid Nimbus config: skip the prompt but still end the stage.
                        promptDetails?.let(navigateToIpProtection)
                        ipProtectionPromptShown = true
                        displayed(message)
                        stopIpProtectionPrompt()
                    },
                    onIneligible = ::leaveIpProtectionStageToStageCompletedMessages,
                    onAlreadySatisfied = ::leaveIpProtectionStageToStageCompletedMessages,
                )
                .also { it.start() }
    }

    /**
     * The prompt won't be shown after all: the stage is ended, with its telemetry, by a stage-completed message
     * instead.
     */
    private fun leaveIpProtectionStageToStageCompletedMessages() {
        stopIpProtectionPrompt()
        evaluate()
    }

    private fun stopIpProtectionPrompt() {
        ipProtectionPrompt?.stop()
        ipProtectionPrompt = null
    }

    private fun showDialog(pageState: OnboardingPageState, onCloseButtonClicked: () -> Unit): Boolean {
        val decorView = activity.window.decorView as? ViewGroup ?: return false
        if (decorView.findViewWithTag<ComposeView>(CONTINUOUS_ONBOARDING_DIALOG_TAG) != null) {
            logger.info("A continuous onboarding card is already shown.")
            return false
        }

        val composeView = ComposeView(activity).apply { tag = CONTINUOUS_ONBOARDING_DIALOG_TAG }
        composeView.setViewCompositionStrategy(ViewCompositionStrategy.DisposeOnDetachedFromWindow)
        composeView.setContent {
            FirefoxTheme {
                ContinuousOnboardingScreen(
                    pageState = pageState,
                    onCloseButtonClicked = onCloseButtonClicked,
                    removeDialogView = { decorView.removeView(composeView) },
                )
            }
        }
        decorView.addView(composeView)
        return true
    }

    companion object {
        /**
         * Registers [ContinuousOnboardingMessagingBinding] with a [Fragment] when continuous onboarding is driven by the
         * Messaging System. Upon destruction of the fragment's view, the binding will be unregistered and all
         * references cleared.
         *
         * @param fragment The [Fragment] to register with.
         * @param wrapper The [ViewBoundFeatureWrapper] to hold the binding in.
         * @param launcher The [ActivityResultLauncher] used to request system roles.
         * @param telemetryRecorder Used to record onboarding telemetry.
         * @param navigateToSyncSignIn Invoked when the user chooses to sign in to Firefox Sync.
         * @param navigateToIpProtection Invoked with the IP Protection onboarding prompt details.
         */
        fun register(
            fragment: Fragment,
            wrapper: ViewBoundFeatureWrapper<ContinuousOnboardingMessagingBinding>,
            launcher: ActivityResultLauncher<Intent>,
            telemetryRecorder: OnboardingTelemetryRecorder,
            navigateToSyncSignIn: () -> Unit,
            navigateToIpProtection: (PromptDetails) -> Unit,
        ) {
            val components = fragment.requireContext().components
            // Remove when ContinuousOnboardingFeature is retired, see Continuous-onboarding-via-messaging.md.
            if (!components.settings.continuousOnboardingViaMessaging) return

            val binding =
                ContinuousOnboardingMessagingBinding(
                    activity = fragment.requireActivity(),
                    launcher = launcher,
                    dependencies =
                        ContinuousOnboardingMessagingDependencies(
                            settings = components.settings,
                            appStore = components.appStore,
                            messaging = components.nimbus.messaging,
                            events = components.nimbus.events,
                            notificationsDelegate = components.notificationsDelegate,
                            ipProtectionStore = components.ipProtection.store,
                            ipProtectionPromptRepository = components.ipProtectionPromptRepository,
                            crashReporter = components.analytics.crashReporter,
                        ),
                    telemetry = ContinuousOnboardingMessageTelemetry(telemetryRecorder),
                    navigateToSyncSignIn = navigateToSyncSignIn,
                    navigateToIpProtection = navigateToIpProtection,
                )
            wrapper.set(feature = binding, owner = fragment.viewLifecycleOwner, view = fragment.requireView())
            fragment.viewLifecycleOwner.lifecycle.addObserver(binding)
        }
    }
}
