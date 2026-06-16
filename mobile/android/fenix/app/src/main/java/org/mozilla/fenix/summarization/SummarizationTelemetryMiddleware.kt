/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.summarization

import mozilla.components.concept.llm.Llm
import mozilla.components.feature.summarize.ContentExtracted
import mozilla.components.feature.summarize.OffDeviceSummarizationShakeConsentAction
import mozilla.components.feature.summarize.OnDeviceSummarizationShakeConsentAction
import mozilla.components.feature.summarize.SummarizationAction
import mozilla.components.feature.summarize.SummarizationCompleted
import mozilla.components.feature.summarize.SummarizationFailed
import mozilla.components.feature.summarize.SummarizationRequested
import mozilla.components.feature.summarize.SummarizationState
import mozilla.components.feature.summarize.SummaryFeedback
import mozilla.components.feature.summarize.SummaryFeedbackProvided
import mozilla.components.feature.summarize.ViewAppeared
import mozilla.components.feature.summarize.ViewDismissed
import mozilla.components.feature.summarize.content.Content
import mozilla.components.lib.state.Middleware
import mozilla.components.lib.state.Store
import mozilla.telemetry.glean.GleanTimerId
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
    val language: String,
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
    private var timerId: GleanTimerId? = null

    override fun invoke(
        store: Store<SummarizationState, SummarizationAction>,
        next: (SummarizationAction) -> Unit,
        action: SummarizationAction,
    ) {
        val stateBefore = store.state
        next(action)

        when (action) {
            ViewAppeared -> handleViewAppeared(stateBefore)
            is SummarizationRequested -> {
                sessionTelemetry = sessionTelemetry.copy(model = action.info.modelId?.value)
            }
            is ContentExtracted -> handleExtractedContent(action.content)
            is SummarizationCompleted -> recordSummarizationCompleted()
            is SummarizationFailed -> recordSummarizationCompleted(success = false, action.exception)
            is SummaryFeedbackProvided -> recordFeedback(action.feedback)
            is ViewDismissed -> {
                AiSummarize.closed.record(
                    AiSummarize.ClosedExtra(
                        model = sessionTelemetry.model,
                        engineAvailable = action.isEngineAvailable,
                    ),
                )

                if (
                    stateBefore is SummarizationState.ShakeConsentRequired ||
                    stateBefore is SummarizationState.ShakeConsentWithDownloadRequired
                ) {
                    AiSummarize.consentDisplayed.record(
                        AiSummarize.ConsentDisplayedExtra(agreed = false),
                    )
                }
            }

            is OnDeviceSummarizationShakeConsentAction.AllowClicked,
            is OffDeviceSummarizationShakeConsentAction.AllowClicked,
            -> {
                AiSummarize.consentDisplayed.record(
                    AiSummarize.ConsentDisplayedExtra(agreed = true),
                )
            }

            is OnDeviceSummarizationShakeConsentAction.CancelClicked,
            is OffDeviceSummarizationShakeConsentAction.CancelClicked,
            -> {
                AiSummarize.consentDisplayed.record(
                    AiSummarize.ConsentDisplayedExtra(agreed = false),
                )
            }

            else -> {}
        }
    }

    private fun handleViewAppeared(stateBefore: SummarizationState) {
        if (stateBefore is SummarizationState.Inert) {
            val trigger = if (stateBefore.initializedWithShake) {
                SummarizationTrigger.SHAKE
            } else {
                SummarizationTrigger.MENU
            }
            sessionTelemetry = sessionTelemetry.copy(trigger = trigger)
        }
        AiSummarize.requested.record(
            AiSummarize.RequestedExtra(trigger = sessionTelemetry.trigger?.toString()),
        )
        timerId = AiSummarize.duration.start()
    }

    private fun handleExtractedContent(content: Content) {
        sessionTelemetry = sessionTelemetry.copy(
            contentMetrics = ContentMetrics(
                wordCount = content.metadata.wordCount,
                charCount = content.body.length,
                contentType = content.metadata.structuredDataTypes.toString(),
                language = content.metadata.language,
            ),
        )
        AiSummarize.started.record(
            AiSummarize.StartedExtra(
                contentType = sessionTelemetry.contentMetrics?.contentType,
                lengthChars = sessionTelemetry.contentMetrics?.charCount,
                lengthWords = sessionTelemetry.contentMetrics?.wordCount,
                model = sessionTelemetry.model,
                trigger = sessionTelemetry.trigger?.toString(),
            ),
        )
    }

    private fun recordFeedback(feedback: SummaryFeedback) {
        val rating = when (feedback) {
            SummaryFeedback.POSITIVE -> "positive"
            SummaryFeedback.NEGATIVE -> "negative"
        }
        AiSummarize.feedback.record(
            AiSummarize.FeedbackExtra(
                model = sessionTelemetry.model,
                rating = rating,
            ),
        )
    }

    /**
     * Identifier for the failure in telemetry. For [Llm.Exception] subtypes we log the qualified
     * class name so provider attribution survives (e.g. MLPA's `RateLimited` vs a hypothetical
     * second provider's `RateLimited`). Bare [Llm.Exception] instances and raw throwables fall
     * back to the underlying cause's simple name, which is more diagnostic than the generic
     * wrapper class.
     */
    private fun Throwable.errorType(): String? = when {
        this::class == Llm.Exception::class -> (cause ?: this)::class.simpleName
        this is Llm.Exception -> this::class.java.name
        else -> (cause ?: this)::class.simpleName
    }

    private fun recordSummarizationCompleted(success: Boolean = true, error: Throwable? = null) {
        timerId?.let {
            AiSummarize.duration.stopAndAccumulate(it)
            timerId = null
        }

        AiSummarize.completed.record(
            AiSummarize.CompletedExtra(
                connectionType = connectionType.toString(),
                contentType = sessionTelemetry.contentMetrics?.contentType,
                errorType = error?.errorType(),
                errorCode = error?.let { ErrorCodeLookup.lookup(it).code },
                language = sessionTelemetry.contentMetrics?.language,
                lengthChars = sessionTelemetry.contentMetrics?.charCount,
                lengthWords = sessionTelemetry.contentMetrics?.wordCount,
                model = sessionTelemetry.model,
                success = success,
                summarizeDurationMs = (System.currentTimeMillis() - sessionTelemetry.startTimeMillis).toInt(),
            ),
        )
    }
}
