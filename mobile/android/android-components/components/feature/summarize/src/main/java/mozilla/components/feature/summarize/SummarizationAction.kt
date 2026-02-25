/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.summarize

import mozilla.components.concept.llm.Llm
import mozilla.components.lib.state.Action

/**
 * Actions for the [SummarizationStore].
 */
interface SummarizationAction : Action

/** The Summarization Screen View Appeared */
data object ViewAppeared : SummarizationAction

/** Shake Consent has been requested */
data object ShakeConsentRequested : SummarizationAction

internal sealed interface LlmProviderAction : SummarizationAction {
    data object ProviderFailed : LlmProviderAction
    data object ProviderUnavailable : LlmProviderAction
    data class ProviderInitialized(val llm: Llm) : LlmProviderAction
}

/** Initialize the Llm */
internal sealed interface LlmAction : SummarizationAction {
    data object SummarizationRequested : SummarizationAction
    data class ReceivedResponse(val response: Llm.Response) : LlmAction
}

/**
 * Actions for the consent step of the shake to summarize user flow when using an on-device model.
 */
sealed interface OnDeviceSummarizationShakeConsentAction : SummarizationAction {
    /** Dispatched when the user taps the "Learn more" link. */
    data object LearnMoreClicked : OnDeviceSummarizationShakeConsentAction

    /** Dispatched when the user grants consent to use the on-device model. */
    data object AllowClicked : OnDeviceSummarizationShakeConsentAction

    /** Dispatched when the user dismisses the consent dialog. */
    data object CancelClicked : OnDeviceSummarizationShakeConsentAction
}

/**
 * Actions for the consent step of the shake to summarize user flow when using an off-device model.
 */
sealed interface OffDeviceSummarizationShakeConsentAction : SummarizationAction {
    /** Dispatched when the user taps the "Learn more" link. */
    data object LearnMoreClicked : OffDeviceSummarizationShakeConsentAction

    /** Dispatched when the user grants consent to use the off-device model. */
    data object AllowClicked : OffDeviceSummarizationShakeConsentAction

    /** Dispatched when the user dismisses the consent dialog. */
    data object CancelClicked : OffDeviceSummarizationShakeConsentAction
}

/**
 * Actions for the consent step of the model download user flow.
 */
sealed interface DownloadConsentAction : SummarizationAction {
    /** Dispatched when the user taps the "Learn more" link. */
    data object LearnMoreClicked : DownloadConsentAction

    /** Dispatched when the user consents to downloading the model. */
    data object AllowClicked : DownloadConsentAction

    /** Dispatched when the user dismisses the download consent dialog. */
    data object CancelClicked : DownloadConsentAction
}

/**
 * Actions for the model download in-progress step of the summarization user flow.
 */
sealed interface DownloadInProgressAction : SummarizationAction {
    /** Dispatched when the user cancels an in-progress model download. */
    data object CancelClicked : DownloadInProgressAction
}

/**
 * Actions for the model download error step of the summarization user flow.
 */
sealed interface DownloadErrorAction : SummarizationAction {
    /** Dispatched when the user taps the "Learn more" link. */
    data object LearnMoreClicked : DownloadErrorAction

    /** Dispatched when the user retries a failed model download. */
    data object TryAgainClicked : DownloadErrorAction

    /** Dispatched when the user dismisses the download error. */
    data object CancelClicked : DownloadErrorAction
}

/**
 * Actions for a general summarization error state.
 */
sealed interface ErrorAction : SummarizationAction {
    /** Dispatched when the user taps the "Learn more" link. */
    data object LearnMoreClicked : ErrorAction
}
