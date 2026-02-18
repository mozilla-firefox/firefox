/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.summarize

import mozilla.components.lib.state.Action

/**
 * Actions for the [SummarizationStore]
 */
internal interface SummarizationAction : Action {
    /**
     * Actions for the consent step of the shake to summarize user flow when using a on-device model
     */
    sealed interface OnDeviceSummarizationShakeConsentAction : SummarizationAction {
        data object LearnMoreClicked : OnDeviceSummarizationShakeConsentAction
        data object AllowClicked : OnDeviceSummarizationShakeConsentAction
        data object CancelClicked : OnDeviceSummarizationShakeConsentAction
    }

    /**
     * Actions for the consent step of the shake to summarize user flow when using a off-device model
     */
    sealed interface OffDeviceSummarizationShakeConsentAction : SummarizationAction {
        data object LearnMoreClicked : OffDeviceSummarizationShakeConsentAction
        data object AllowClicked : OffDeviceSummarizationShakeConsentAction
        data object CancelClicked : OffDeviceSummarizationShakeConsentAction
    }

    sealed interface DownloadConsentAction : SummarizationAction {
        data object LearnMoreClicked : DownloadConsentAction
        data object AllowClicked : DownloadConsentAction
        data object CancelClicked : DownloadConsentAction
    }

    sealed interface DownloadInProgressAction : SummarizationAction {
        data object CancelClicked : DownloadInProgressAction
    }

    sealed interface DownloadErrorAction : SummarizationAction {
        data object LearnMoreClicked : DownloadErrorAction
        data object TryAgainClicked : DownloadErrorAction
        data object CancelClicked : DownloadErrorAction
    }

    sealed interface ErrorAction : SummarizationAction {
        data object LearnMoreClicked : ErrorAction
    }
}
