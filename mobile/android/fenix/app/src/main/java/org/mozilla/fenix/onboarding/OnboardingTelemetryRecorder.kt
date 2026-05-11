/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.onboarding

import mozilla.components.support.base.log.logger.Logger
import org.mozilla.fenix.GleanMetrics.Onboarding
import org.mozilla.fenix.GleanMetrics.Pings
import org.mozilla.fenix.GleanMetrics.TermsOfUse
import org.mozilla.fenix.onboarding.view.OnboardingPageUiData
import org.mozilla.fenix.termsofuse.TOU_VERSION
import org.mozilla.fenix.termsofuse.store.Surface

/**
 * Abstraction responsible for recording telemetry events for Onboarding.
 */
class OnboardingTelemetryRecorder(
    private val onboardingReason: OnboardingReason,
    private val installSource: String,
) {
    val logger = Logger("OnboardingTelemetryRecorder")

    /**
     * Records "onboarding_completed" telemetry event and sends the onboarding ping.
     * @param sequenceId The identifier of the onboarding sequence shown to the user.
     * @param sequencePosition The sequence position of the page on which the completed event occurred.
     * @param dismissedMethod The method used to dismiss the onboarding flow.
     */
    fun onOnboardingComplete(
        sequenceId: String,
        sequencePosition: String,
        dismissedMethod: DismissedMethod = DismissedMethod.COMPLETED,
        ) {
        logger.debug(
            "Recording onboarding completed event, sequenceId: $sequenceId, " +
                "sequencePosition: $sequencePosition, dismissedMethod: $dismissedMethod",
        )

        Onboarding.completed.record(
            Onboarding.CompletedExtra(
                sequenceId = sequenceId,
                sequencePosition = sequencePosition,
                onboardingReason = onboardingReason.value,
                installSource = installSource,
            ),
        )
        Onboarding.dismissed.record(
            Onboarding.DismissedExtra(
                method = dismissedMethod.telemetryId,
                onboardingReason = onboardingReason.value,
                installSource = installSource,
            ),
        )
        Pings.onboarding.submit()
    }

    /**
     * Records "onboarding_started" telemetry event.
     */
    fun onOnboardingStarted() {
        Onboarding.started.record(
            extra = Onboarding.StartedExtra(
                onboardingReason = onboardingReason.value,
                installSource = installSource,
            ),
        )
    }

    /**
     * Records impression events for a given [OnboardingPageUiData.Type].
     * @param sequenceId The identifier of the onboarding sequence shown to the user.
     * @param pageType The page type for which the impression occurred.
     * @param sequencePosition The sequence position of the page for which the impression occurred.
     */
    @Suppress("LongMethod")
    fun onImpression(
        sequenceId: String,
        pageType: OnboardingPageUiData.Type,
        sequencePosition: String,
    ) {
        logger.debug(
            "Recording on impression event, sequenceId: $sequenceId, " +
                "pageType: $pageType, sequencePosition: $sequencePosition",
        )

        when (pageType) {
            OnboardingPageUiData.Type.DEFAULT_BROWSER -> {
                Onboarding.setToDefaultCard.record(
                    Onboarding.SetToDefaultCardExtra(
                        action = ACTION_IMPRESSION,
                        elementType = ET_ONBOARDING_CARD,
                        sequenceId = sequenceId,
                        sequencePosition = sequencePosition,
                        onboardingReason = onboardingReason.value,
                        installSource = installSource,
                    ),
                )
            }

            OnboardingPageUiData.Type.ADD_SEARCH_WIDGET -> {
                Onboarding.addSearchWidgetCard.record(
                    Onboarding.AddSearchWidgetCardExtra(
                        action = ACTION_IMPRESSION,
                        elementType = ET_ONBOARDING_CARD,
                        sequenceId = sequenceId,
                        sequencePosition = sequencePosition,
                        onboardingReason = onboardingReason.value,
                        installSource = installSource,
                    ),
                )
            }

            OnboardingPageUiData.Type.SYNC_SIGN_IN -> {
                Onboarding.signInCard.record(
                    Onboarding.SignInCardExtra(
                        action = ACTION_IMPRESSION,
                        elementType = ET_ONBOARDING_CARD,
                        sequenceId = sequenceId,
                        sequencePosition = sequencePosition,
                        onboardingReason = onboardingReason.value,
                        installSource = installSource,
                    ),
                )
            }

            OnboardingPageUiData.Type.NOTIFICATION_PERMISSION -> {
                Onboarding.turnOnNotificationsCard.record(
                    Onboarding.TurnOnNotificationsCardExtra(
                        action = ACTION_IMPRESSION,
                        elementType = ET_ONBOARDING_CARD,
                        sequenceId = sequenceId,
                        sequencePosition = sequencePosition,
                        onboardingReason = onboardingReason.value,
                        installSource = installSource,
                    ),
                )
            }

            OnboardingPageUiData.Type.TOOLBAR_PLACEMENT -> {
                Onboarding.toolbarPlacementCard.record(
                    Onboarding.ToolbarPlacementCardExtra(
                        action = ACTION_IMPRESSION,
                        elementType = ET_ONBOARDING_CARD,
                        sequenceId = sequenceId,
                        sequencePosition = sequencePosition,
                        onboardingReason = onboardingReason.value,
                        installSource = installSource,
                    ),
                )
            }

            OnboardingPageUiData.Type.THEME_SELECTION -> {
                Onboarding.themeSelectionCard.record(
                    Onboarding.ThemeSelectionCardExtra(
                        action = ACTION_IMPRESSION,
                        elementType = ET_ONBOARDING_CARD,
                        sequenceId = sequenceId,
                        sequencePosition = sequencePosition,
                        onboardingReason = onboardingReason.value,
                        installSource = installSource,
                    ),
                )
            }

            OnboardingPageUiData.Type.TERMS_OF_SERVICE -> {
                Onboarding.termsOfServiceCard.record(
                    Onboarding.TermsOfServiceCardExtra(
                        action = ACTION_IMPRESSION,
                        elementType = ET_ONBOARDING_CARD,
                        sequenceId = sequenceId,
                        sequencePosition = sequencePosition,
                        onboardingReason = onboardingReason.value,
                        installSource = installSource,
                    ),
                )
            }

            OnboardingPageUiData.Type.MARKETING_DATA -> {
                Onboarding.marketingDataCardViewed.record(
                    Onboarding.MarketingDataCardViewedExtra(
                        action = ACTION_IMPRESSION,
                        elementType = ET_ONBOARDING_CARD,
                        sequenceId = sequenceId,
                        sequencePosition = sequencePosition,
                        onboardingReason = onboardingReason.value,
                        installSource = installSource,
                    ),
                )
            }
        }
    }

    /**
     * Records set to default click event.
     * @param sequenceId The identifier of the onboarding sequence shown to the user.
     * @param sequencePosition The sequence position of the page for which the impression occurred.
     */
    fun onSetToDefaultClick(sequenceId: String, sequencePosition: String) {
        logger.debug(
            "Recording set to default click event, sequenceId: $sequenceId, " +
                "sequencePosition: $sequencePosition",
        )

        Onboarding.setToDefault.record(
            Onboarding.SetToDefaultExtra(
                action = ACTION_CLICK,
                elementType = ET_PRIMARY_BUTTON,
                sequenceId = sequenceId,
                sequencePosition = sequencePosition,
                onboardingReason = onboardingReason.value,
                installSource = installSource,
            ),
        )
    }

    /**
     * Records sync sign in click event.
     * @param sequenceId The identifier of the onboarding sequence shown to the user.
     * @param sequencePosition The sequence position of the page for which the impression occurred.
     */
    fun onSyncSignInClick(sequenceId: String, sequencePosition: String) {
        logger.debug(
            "Recording sync sign in click event, sequenceId: $sequenceId, " +
                "sequencePosition: $sequencePosition",
        )

        Onboarding.signIn.record(
            Onboarding.SignInExtra(
                action = ACTION_CLICK,
                elementType = ET_PRIMARY_BUTTON,
                sequenceId = sequenceId,
                sequencePosition = sequencePosition,
                onboardingReason = onboardingReason.value,
                installSource = installSource,
            ),
        )
    }

    /**
     * Records notification permission click event.
     * @param sequenceId The identifier of the onboarding sequence shown to the user.
     * @param sequencePosition The sequence position of the page for which the impression occurred.
     */
    fun onNotificationPermissionClick(sequenceId: String, sequencePosition: String) {
        logger.debug(
            "Recording notification permission click event, sequenceId: $sequenceId, " +
                "sequencePosition: $sequencePosition",
        )

        Onboarding.turnOnNotifications.record(
            Onboarding.TurnOnNotificationsExtra(
                action = ACTION_CLICK,
                elementType = ET_PRIMARY_BUTTON,
                sequenceId = sequenceId,
                sequencePosition = sequencePosition,
                onboardingReason = onboardingReason.value,
                installSource = installSource,
            ),
        )
    }

    /**
     * Records add search widget click event.
     * @param sequenceId The identifier of the onboarding sequence shown to the user.
     * @param sequencePosition The sequence position of the page for which the impression occurred.
     */
    fun onAddSearchWidgetClick(sequenceId: String, sequencePosition: String) {
        Onboarding.addSearchWidget.record(
            Onboarding.AddSearchWidgetExtra(
                action = ACTION_CLICK,
                elementType = ET_PRIMARY_BUTTON,
                sequenceId = sequenceId,
                sequencePosition = sequencePosition,
                onboardingReason = onboardingReason.value,
                installSource = installSource,
            ),
        )
    }

    /**
     * Records skip set to default click event.
     * @param sequenceId The identifier of the onboarding sequence shown to the user.
     * @param sequencePosition The sequence position of the page for which the impression occurred.
     */
    fun onSkipSetToDefaultClick(sequenceId: String, sequencePosition: String) {
        Onboarding.skipDefault.record(
            Onboarding.SkipDefaultExtra(
                action = ACTION_CLICK,
                elementType = ET_SECONDARY_BUTTON,
                sequenceId = sequenceId,
                sequencePosition = sequencePosition,
                onboardingReason = onboardingReason.value,
                installSource = installSource,
            ),
        )
    }

    /**
     * Records skip sign in click event.
     * @param sequenceId The identifier of the onboarding sequence shown to the user.
     * @param sequencePosition The sequence position of the page for which the impression occurred.
     * @param elementType The type of UI element that triggered the click event.
     */
    fun onSkipSignInClick(
        sequenceId: String,
        sequencePosition: String,
        elementType: String = ET_SECONDARY_BUTTON,
    ) {
        logger.debug(
            "Recording skip sign in click event, sequenceId: $sequenceId, " +
                "sequencePosition: $sequencePosition, elementType: $elementType",
        )

        Onboarding.skipSignIn.record(
            Onboarding.SkipSignInExtra(
                action = ACTION_CLICK,
                elementType = elementType,
                sequenceId = sequenceId,
                sequencePosition = sequencePosition,
                onboardingReason = onboardingReason.value,
                installSource = installSource,
            ),
        )
    }

    /**
     * Records skip add widget click event.
     * @param sequenceId The identifier of the onboarding sequence shown to the user.
     * @param sequencePosition The sequence position of the page for which the impression occurred.
     */
    fun onSkipAddWidgetClick(sequenceId: String, sequencePosition: String) {
        Onboarding.skipAddSearchWidget.record(
            Onboarding.SkipAddSearchWidgetExtra(
                action = ACTION_CLICK,
                elementType = ET_SECONDARY_BUTTON,
                sequenceId = sequenceId,
                sequencePosition = sequencePosition,
                onboardingReason = onboardingReason.value,
                installSource = installSource,
            ),
        )
    }

    /**
     * Records skip notification permission click event.
     * @param sequenceId The identifier of the onboarding sequence shown to the user.
     * @param sequencePosition The sequence position of the page for which the impression occurred.
     * @param elementType The type of UI element that triggered the click event.
     */
    fun onSkipTurnOnNotificationsClick(
        sequenceId: String,
        sequencePosition: String,
        elementType: String = ET_SECONDARY_BUTTON,
    ) {
        logger.debug(
            "Recording skip notification permission click event, " +
                "sequenceId: $sequenceId, sequencePosition: $sequencePosition, elementType: $elementType",
        )

        Onboarding.skipTurnOnNotifications.record(
            Onboarding.SkipTurnOnNotificationsExtra(
                action = ACTION_CLICK,
                elementType = elementType,
                sequenceId = sequenceId,
                sequencePosition = sequencePosition,
                onboardingReason = onboardingReason.value,
                installSource = installSource,
            ),
        )
    }

    /**
     * Records select toolbar placement click event.
     * @param sequenceId The identifier of the onboarding sequence shown to the user.
     * @param sequencePosition The sequence position of the page for which the impression occurred.
     * @param toolbarPlacement The toolbar placement option chosen by the user.
     */
    fun onSelectToolbarPlacementClick(sequenceId: String, sequencePosition: String, toolbarPlacement: String) {
        Onboarding.selectToolbarPlacement.record(
            Onboarding.SelectToolbarPlacementExtra(
                action = ACTION_CLICK,
                elementType = ET_PRIMARY_BUTTON,
                sequenceId = sequenceId,
                sequencePosition = sequencePosition,
                toolbarPlacement = toolbarPlacement,
                onboardingReason = onboardingReason.value,
                installSource = installSource,
            ),
        )
    }

    /**
     * Records select theme click event.
     * @param themeOption The selected theme option ("dark", "light", or "system").
     * @param sequenceId The identifier of the onboarding sequence shown to the user.
     * @param sequencePosition The sequence position of the page for which the impression occurred.
     */
    fun onSelectThemeClick(themeOption: String, sequenceId: String, sequencePosition: String) {
        Onboarding.selectTheme.record(
            Onboarding.SelectThemeExtra(
                action = ACTION_CLICK,
                themeOption = themeOption,
                sequenceId = sequenceId,
                sequencePosition = sequencePosition,
                onboardingReason = onboardingReason.value,
                installSource = installSource,
            ),
        )
    }

    /**
     * Records privacy policy link text click event.
     * @param sequenceId The identifier of the onboarding sequence shown to the user.
     * @param sequencePosition The sequence position of the page on which the link click event occurred.
     */
    fun onPrivacyPolicyClick(sequenceId: String, sequencePosition: String) {
        Onboarding.privacyPolicy.record(
            Onboarding.PrivacyPolicyExtra(
                action = ACTION_CLICK,
                elementType = ET_SECONDARY_BUTTON,
                sequenceId = sequenceId,
                sequencePosition = sequencePosition,
                onboardingReason = onboardingReason.value,
                installSource = installSource,
            ),
        )
    }

    /**
     * Records when the terms of service link is clicked.
     */
    fun onTermsOfServiceLinkClick() {
        Onboarding.termsOfServiceLinkClicked.record(
            extra = Onboarding.TermsOfServiceLinkClickedExtra(
                onboardingReason = onboardingReason.value,
                installSource = installSource,
            ),
        )
    }

    /**
     * Records when the privacy notice link clicked.
     */
    fun onTermsOfServicePrivacyNoticeLinkClick() {
        Onboarding.termsOfServicePrivacyNoticeLinkClicked.record(
            extra = Onboarding.TermsOfServicePrivacyNoticeLinkClickedExtra(
                onboardingReason = onboardingReason.value,
                installSource = installSource,
            ),
        )
    }

    /**
     * Records when the manage privacy preferences link clicked.
     */
    fun onTermsOfServiceManagePrivacyPreferencesLinkClick() {
        Onboarding.termsOfServiceManageLinkClicked.record(
            extra = Onboarding.TermsOfServiceManageLinkClickedExtra(
                onboardingReason = onboardingReason.value,
                installSource = installSource,
            ),
        )
    }

    /**
     * Records when the accept terms button clicked.
     */
    fun onTermsOfServiceManagerAcceptTermsButtonClick() {
        Onboarding.shown.record(
            extra = Onboarding.ShownExtra(
                onboardingReason = onboardingReason.value,
                installSource = installSource,
            ),
        )
        Onboarding.termsOfServiceAccepted.record(
            extra = Onboarding.TermsOfServiceAcceptedExtra(
                onboardingReason = onboardingReason.value,
                installSource = installSource,
            ),
        )
        TermsOfUse.accepted.record(
            TermsOfUse.AcceptedExtra(
                surface = Surface.ONBOARDING.metricLabel,
                touVersion = TOU_VERSION,
            ),
        )
        TermsOfUse.version.set(TOU_VERSION.toLong())
        TermsOfUse.date.set()
    }

    /**
     * Records the marketing data card continue button click event
     * @param optIn If the user chose to opt in to marketing data collection
     */
    fun onMarketingDataContinueClicked(optIn: Boolean) {
        Onboarding.marketingDataContinueClicked.record(
            Onboarding.MarketingDataContinueClickedExtra(
                optIn = optIn,
                onboardingReason = onboardingReason.value,
                installSource = installSource,
            ),
        )
    }

    /**
     * Records the marketing data card skip button click event.
     */
    fun onMarketingDataSkipClicked() {
        Onboarding.marketingDataSkipClicked.record(
            Onboarding.MarketingDataSkipClickedExtra(
                onboardingReason = onboardingReason.value,
                installSource = installSource,
            ),
        )
    }

    /**
     * Records when the marketing data learn more link clicked.
     */
    fun onMarketingDataLearnMoreClick() = Onboarding.marketingDataLearnMore.record(
        extra = Onboarding.MarketingDataLearnMoreExtra(
            onboardingReason = onboardingReason.value,
            installSource = installSource,
        ),
    )

    /**
     * Records the marketing data opt-in toggle event.
     * @param optIn True if the user chose to opt in to marketing data collection.
     */
    fun onMarketingDataOptInToggled(optIn: Boolean) {
        Onboarding.marketingDataOptInToggled.record(
            Onboarding.MarketingDataOptInToggledExtra(
                optIn = optIn,
                onboardingReason = onboardingReason.value,
                installSource = installSource,
            ),
        )
    }

    /**
     * Sends the onboarding ping when the user navigates to the next onboarding page.
     */
    fun onNavigatedToNextPage() {
        Pings.onboarding.submit()
    }

    companion object {
        private const val ACTION_IMPRESSION = "impression"
        private const val ACTION_CLICK = "click"
        private const val ET_ONBOARDING_CARD = "onboarding_card"
        private const val ET_PRIMARY_BUTTON = "primary_button"
        private const val ET_SECONDARY_BUTTON = "secondary_button"
        const val ET_CARD_CLOSE_BUTTON = "card_close_button"
    }
}

/**
 * Enum representing the method used to dismiss the onboarding flow.
 * @property telemetryId The telemetry identifier.
 *
 * @see [Onboarding.DismissedExtra.method].
 */
enum class DismissedMethod(val telemetryId: String) {
    COMPLETED("completed"),
    SKIPPED("skipped"),
}
