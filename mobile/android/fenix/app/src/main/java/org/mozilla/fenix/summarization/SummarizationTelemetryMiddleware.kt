/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.summarization

import mozilla.components.concept.llm.Llm
import mozilla.components.feature.summarize.LlmAction
import mozilla.components.feature.summarize.LlmProviderAction
import mozilla.components.feature.summarize.ShakeConsentRequested
import mozilla.components.feature.summarize.SummarizationAction
import mozilla.components.feature.summarize.SummarizationFailed
import mozilla.components.feature.summarize.SummarizationState
import mozilla.components.feature.summarize.ViewAppeared
import mozilla.components.feature.summarize.ViewDismissed
import mozilla.components.lib.state.Middleware
import mozilla.components.lib.state.Store
import mozilla.telemetry.glean.private.NoExtras
import org.mozilla.fenix.GleanMetrics.AiSummarize

/**
 * Represents a full summarization session aggregation of telemetry data
 */
private data class SummarizationSessionTelemetry(
    val trigger: SummarizationTrigger? = null,
    val model: String? = null,
    val startTimeMillis: Long = System.currentTimeMillis(),
    val contentMetrics: ContentMetrics? = null,
)

/**
 * Metrics representing the length/size of the content.
 */
private data class ContentMetrics(
    val wordCount: Int,
    val charCount: Int,
    val contentType: String? = null,
)

/**
 * Defines how the user initiated the summarization.
 */
private enum class SummarizationTrigger {
    SHAKE, MENU
}

/**
 * The type of network connection available on the device.
 */
enum class ConnectionType {
    WIFI, CELLULAR, OTHER, NONE
}

/**
 * @param connectionType current network [ConnectionType].
 */
class SummarizationTelemetryMiddleware(
    private val connectionType: ConnectionType,
) : Middleware<SummarizationState, SummarizationAction> {

    private var sessionTelemetry = SummarizationSessionTelemetry()

    override fun invoke(
        store: Store<SummarizationState, SummarizationAction>,
        next: (SummarizationAction) -> Unit,
        action: SummarizationAction,
    ) {
        val stateBefore = store.state
        next(action)

        when (action) {
            ViewAppeared -> handleViewAppeared(stateBefore)
            is LlmAction.SummarizationRequested -> {
                sessionTelemetry = sessionTelemetry.copy(model = action.info.nameRes.toString())
            }
            is LlmAction.LlmPrompted -> handleLlmPrompted(action)
            is LlmAction.ReceivedResponse -> handleReceivedResponse(action)
            ViewDismissed -> {
                AiSummarize.summarizationClosed.record(
                    AiSummarize.SummarizationClosedExtra(
                        model = sessionTelemetry.model,
                    ),
                )
            }
            ShakeConsentRequested -> {
                AiSummarize.summarizationConsentDisplayed.record(AiSummarize.SummarizationConsentDisplayedExtra())
            }

//            is OnDeviceSummarizationShakeConsentAction.AllowClicked,
//            is OffDeviceSummarizationShakeConsentAction.AllowClicked,
//            -> {
//                AiSummarize.summarizationConsentDisplayed.record(
//                    AiSummarize.SummarizationConsentDisplayedExtra(agreed = true),
//                )
//            }
//
//            is OnDeviceSummarizationShakeConsentAction.CancelClicked,
//            is OffDeviceSummarizationShakeConsentAction.CancelClicked,
//            -> {
//                AiSummarize.summarizationConsentDisplayed.record(
//                    AiSummarize.SummarizationConsentDisplayedExtra(agreed = false),
//                )
//            }

            is SummarizationFailed -> AiSummarize.summaryFailure.record(NoExtras())
            is LlmProviderAction.ProviderFailed,
            is LlmProviderAction.ProviderUnavailable,
            -> AiSummarize.summaryFailure.record(NoExtras())
            else -> {}
        }
    }

    private fun handleViewAppeared(stateBefore: SummarizationState) {
        AiSummarize.summarizationRequested.record(NoExtras())
        if (stateBefore is SummarizationState.Inert) {
            val trigger = if (stateBefore.initializedWithShake) {
                SummarizationTrigger.SHAKE
            } else {
                SummarizationTrigger.MENU
            }
            sessionTelemetry = sessionTelemetry.copy(trigger = trigger)
        }
    }

    private fun handleLlmPrompted(action: LlmAction.LlmPrompted) {
        sessionTelemetry = sessionTelemetry.copy(
            contentMetrics = ContentMetrics(
                wordCount = action.content.trim().split(Regex("\\s+")).size,
                charCount = action.content.length,
                contentType = action.pageMetadata?.structuredDataTypes?.toString(),
            ),
        )
        AiSummarize.summarizationStarted.record(
            AiSummarize.SummarizationStartedExtra(
                contentType = sessionTelemetry.contentMetrics?.contentType,
                lengthChars = sessionTelemetry.contentMetrics?.charCount,
                lengthWords = sessionTelemetry.contentMetrics?.wordCount,
                model = sessionTelemetry.model,
                trigger = sessionTelemetry.trigger?.toString(),
            ),
        )
    }

    private fun handleReceivedResponse(action: LlmAction.ReceivedResponse) {
        when (action.response) {
            is Llm.Response.Success.ReplyFinished -> {
                recordSummarizationCompleted(outcome = true, errorType = null)
            }
            is Llm.Response.Failure -> {
                recordSummarizationCompleted(
                    outcome = false,
                    errorType = (action.response as Llm.Response.Failure).reason,
                )
                AiSummarize.summaryFailure.record(NoExtras())
            }
            else -> {}
        }
    }

    private fun recordSummarizationCompleted(outcome: Boolean, errorType: String?) {
        AiSummarize.summarizationCompleted.record(
            AiSummarize.SummarizationCompletedExtra(
                connectionType = connectionType.toString(),
                contentType = sessionTelemetry.contentMetrics?.contentType,
                errorType = errorType,
                lengthChars = sessionTelemetry.contentMetrics?.charCount,
                lengthWords = sessionTelemetry.contentMetrics?.wordCount,
                model = sessionTelemetry.model.toString(),
                outcome = outcome,
                time = (System.currentTimeMillis() - sessionTelemetry.startTimeMillis).toInt(),
            ),
        )
    }
}
