/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.summarize

import mozilla.components.feature.summarize.settings.summarizeSettingsReducer
import mozilla.components.ui.richtext.ir.RichDocument

/**
 * Reduces the given [action] and current [state] into a new [SummarizationState].
 *
 * @param state The current [SummarizationState].
 * @param action The [SummarizationAction] to process.
 * @return The resulting [SummarizationState] after applying the action.
 */
fun summarizationReducer(state: SummarizationState, action: SummarizationAction) =
    when (action) {
        is ShakeConsentRequested -> SummarizationState.ShakeConsentRequired
        SignInSummarizationContentAction.DismissClicked,
        OffDeviceSummarizationShakeConsentAction.CancelClicked -> SummarizationState.Finished.Cancelled
        SignInSummarizationContentAction.LearnMoreClicked -> SummarizationState.LearnMoreAboutCloudSupportedFeatures
        SignInSummarizationContentAction.SignInClicked -> SummarizationState.Finished.NavigatedToSignIn
        OffDeviceSummarizationShakeConsentAction.LearnMoreClicked -> SummarizationState.LearnMoreAboutShakeConsent
        OnDeviceSummarizationShakeConsentAction.LearnMoreClicked -> SummarizationState.LearnMoreAboutShakeConsent
        ErrorAction.ErrorDismissed -> SummarizationState.Finished.ErrorDismissed
        PageLoadStarted -> SummarizationState.PageLoading
        is LlmProviderAction.SignInRequired -> SummarizationState.SignInRequired
        is SummarizationRequested -> SummarizationState.Loading(action.info)
        is SummarizationCompleted -> state.complete()
        is SummarizationFailed -> SummarizationState.Error(SummarizationError.SummarizationFailed(action.exception))
        is ReceivedParsedDocument -> state.updateDocument(action.document)
        is SettingsLoaded ->
            when (state) {
                is SummarizationState.Summarized ->
                    SummarizationState.Settings(
                        info = state.info,
                        document = state.document,
                        settingsState = action.settings,
                    )
                else -> state
            }
        is SettingsBackClicked ->
            when (state) {
                is SummarizationState.Settings ->
                    SummarizationState.Summarized(info = state.info, document = state.document)
                else -> state
            }
        is SummarizeSettingsActionWrapper ->
            when (state) {
                is SummarizationState.Settings ->
                    state.copy(settingsState = summarizeSettingsReducer(state.settingsState, action.inner))
                else -> state
            }

        is ContentExtracted,
        DownloadConsentAction.AllowClicked,
        DownloadConsentAction.CancelClicked,
        DownloadConsentAction.LearnMoreClicked,
        DownloadErrorAction.CancelClicked,
        DownloadErrorAction.LearnMoreClicked,
        DownloadErrorAction.TryAgainClicked,
        DownloadInProgressAction.CancelClicked,
        ErrorAction.LearnMoreClicked,
        is LlmProviderAction.ProviderInitialized,
        OffDeviceSummarizationShakeConsentAction.AllowClicked,
        OnDeviceSummarizationShakeConsentAction.AllowClicked,
        OnDeviceSummarizationShakeConsentAction.CancelClicked,
        PageLoadCompleted,
        SettingsClicked,
        SignInSummarizationContentAction.DismissClicked,
        SignInSummarizationContentAction.LearnMoreClicked,
        SignInSummarizationContentAction.SignInClicked,
        ViewAppeared,
        is ViewDismissed -> state
    }

private fun SummarizationState.complete(): SummarizationState {
    if (this !is SummarizationState.Summarizing) return this
    return SummarizationState.Summarized(info, document)
}

internal fun SummarizationState.updateDocument(document: RichDocument): SummarizationState {
    return when (this) {
        is SummarizationState.Loading -> SummarizationState.Summarizing(info, document)
        is SummarizationState.Summarizing -> copy(document = document)
        else -> this
    }
}
