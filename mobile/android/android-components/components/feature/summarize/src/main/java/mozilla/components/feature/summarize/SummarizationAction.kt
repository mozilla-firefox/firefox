/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.summarize

import mozilla.components.concept.llm.Llm
import mozilla.components.concept.llm.LlmProvider
import mozilla.components.feature.summarize.content.Content
import mozilla.components.feature.summarize.settings.SummarizeSettingsAction
import mozilla.components.feature.summarize.settings.SummarizeSettingsState
import mozilla.components.lib.state.Action
import mozilla.components.ui.richtext.ir.RichDocument

/** Actions for the [SummarizationStore]. */
sealed interface SummarizationAction : Action

/** The Summarization Screen View Appeared */
data object ViewAppeared : SummarizationAction

/** The Summarization Screen View was Dismissed */
data class ViewDismissed(val isEngineAvailable: Boolean) : SummarizationAction

/** The browser page has just started to load and summarization is not available yet. */
data object PageLoadStarted : SummarizationAction

/** The browser page has just finished loading. */
data object PageLoadCompleted : SummarizationAction

/**
 * The user tapped the settings cog. [SummarizationSettingsWrapperMiddleware] reacts by dispatching [SettingsLoaded]
 * carrying the persisted preferences, so without that middleware installed this is a no-op: the UI knows only that the
 * cog was tapped, not what to show.
 */
data object SettingsClicked : SummarizationAction

/**
 * The persisted preferences [settings] are available and the settings screen can be shown.
 *
 * Dispatched only by [SummarizationSettingsWrapperMiddleware], so that the persisted preferences are carried into the
 * transition itself and the screen is never composed with the defaults.
 */
data class SettingsLoaded(val settings: SummarizeSettingsState) : SummarizationAction

/** The user tapped the back button from settings. */
data object SettingsBackClicked : SummarizationAction

/**
 * A wrapper for delegating [SummarizeSettingsAction]s to the settings sub-state of [SummarizationState.Settings] and to
 * [SummarizationSettingsWrapperMiddleware].
 */
data class SummarizeSettingsActionWrapper(val inner: SummarizeSettingsAction) : SummarizationAction

/** Shake Consent has been requested */
data object ShakeConsentRequested : SummarizationAction

/** Actions related to preparing and initializing the LLM provider. */
sealed interface LlmProviderAction : SummarizationAction {

    /**
     * Preparing the provider failed because the user must sign in. Drives the sign-in UI.
     *
     * @property reason The provider-unavailable exception that blocked preparation, carried through for telemetry.
     */
    data class SignInRequired(val reason: Throwable) : LlmProviderAction

    /** The LLM provider finished initializing with the given [llm]. */
    data class ProviderInitialized(val llm: Llm) : LlmProviderAction
}

/** There was a failure in summarizing content from the current page. */
data class SummarizationFailed(val exception: Throwable) : SummarizationAction

/** We've requested a response from a Llm. */
data class SummarizationRequested(val info: LlmProvider.Info) : SummarizationAction

/** The Summarization has completed successfully. */
data object SummarizationCompleted : SummarizationAction

/** We've received a new parsed document. */
data class ReceivedParsedDocument(val document: RichDocument) : SummarizationAction

/** Page content has been extracted and is ready to be sent to the LLM. */
data class ContentExtracted(val content: Content) : SummarizationAction

/** Actions for the consent step of the shake to summarize user flow when using an on-device model. */
sealed interface OnDeviceSummarizationShakeConsentAction : SummarizationAction {
    /** Dispatched when the user taps the "Learn more" link. */
    data object LearnMoreClicked : OnDeviceSummarizationShakeConsentAction

    /** Dispatched when the user grants consent to use the on-device model. */
    data object AllowClicked : OnDeviceSummarizationShakeConsentAction

    /** Dispatched when the user dismisses the consent dialog. */
    data object CancelClicked : OnDeviceSummarizationShakeConsentAction
}

/** Actions for the consent step of the shake to summarize user flow when using an off-device model. */
sealed interface OffDeviceSummarizationShakeConsentAction : SummarizationAction {
    /** Dispatched when the user taps the "Learn more" link. */
    data object LearnMoreClicked : OffDeviceSummarizationShakeConsentAction

    /** Dispatched when the user grants consent to use the off-device model. */
    data object AllowClicked : OffDeviceSummarizationShakeConsentAction

    /** Dispatched when the user dismisses the consent dialog. */
    data object CancelClicked : OffDeviceSummarizationShakeConsentAction
}

/** Actions for the sign-in content shown when an integrity failure blocks summarization. */
sealed interface SignInSummarizationContentAction : SummarizationAction {
    /** Dispatched when the user taps the "Learn more" link. */
    data object LearnMoreClicked : SignInSummarizationContentAction

    /** Dispatched when the user taps the sign-in button. */
    data object SignInClicked : SignInSummarizationContentAction

    /** Dispatched when the user dismisses the sign-in prompt. */
    data object DismissClicked : SignInSummarizationContentAction
}

/** Actions for the consent step of the model download user flow. */
sealed interface DownloadConsentAction : SummarizationAction {
    /** Dispatched when the user taps the "Learn more" link. */
    data object LearnMoreClicked : DownloadConsentAction

    /** Dispatched when the user consents to downloading the model. */
    data object AllowClicked : DownloadConsentAction

    /** Dispatched when the user dismisses the download consent dialog. */
    data object CancelClicked : DownloadConsentAction
}

/** Actions for the model download in-progress step of the summarization user flow. */
sealed interface DownloadInProgressAction : SummarizationAction {
    /** Dispatched when the user cancels an in-progress model download. */
    data object CancelClicked : DownloadInProgressAction
}

/** Actions for the model download error step of the summarization user flow. */
sealed interface DownloadErrorAction : SummarizationAction {
    /** Dispatched when the user taps the "Learn more" link. */
    data object LearnMoreClicked : DownloadErrorAction

    /** Dispatched when the user retries a failed model download. */
    data object TryAgainClicked : DownloadErrorAction

    /** Dispatched when the user dismisses the download error. */
    data object CancelClicked : DownloadErrorAction
}

/** Actions for a general summarization error state. */
sealed interface ErrorAction : SummarizationAction {
    /** Dispatched when the user taps the "Learn more" link. */
    data object LearnMoreClicked : ErrorAction

    /** Dispatched when the user taps the the "Dismiss" button on the error screen. */
    data object ErrorDismissed : ErrorAction
}
