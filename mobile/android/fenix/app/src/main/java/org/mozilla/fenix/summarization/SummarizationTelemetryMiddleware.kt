/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.summarization

import java.util.UUID
import mozilla.components.concept.llm.Llm
import mozilla.components.feature.summarize.ContentExtracted
import mozilla.components.feature.summarize.DownloadConsentAction
import mozilla.components.feature.summarize.DownloadErrorAction
import mozilla.components.feature.summarize.DownloadInProgressAction
import mozilla.components.feature.summarize.ErrorAction
import mozilla.components.feature.summarize.LlmProviderAction
import mozilla.components.feature.summarize.OffDeviceSummarizationShakeConsentAction
import mozilla.components.feature.summarize.OnDeviceSummarizationShakeConsentAction
import mozilla.components.feature.summarize.PageLoadCompleted
import mozilla.components.feature.summarize.PageLoadStarted
import mozilla.components.feature.summarize.ReceivedParsedDocument
import mozilla.components.feature.summarize.SettingsBackClicked
import mozilla.components.feature.summarize.SettingsClicked
import mozilla.components.feature.summarize.SettingsLoaded
import mozilla.components.feature.summarize.ShakeConsentRequested
import mozilla.components.feature.summarize.SignInSummarizationContentAction
import mozilla.components.feature.summarize.SummarizationAction
import mozilla.components.feature.summarize.SummarizationCompleted
import mozilla.components.feature.summarize.SummarizationFailed
import mozilla.components.feature.summarize.SummarizationRequested
import mozilla.components.feature.summarize.SummarizationState
import mozilla.components.feature.summarize.SummarizeSettingsActionWrapper
import mozilla.components.feature.summarize.ViewAppeared
import mozilla.components.feature.summarize.ViewDismissed
import mozilla.components.feature.summarize.content.Content
import mozilla.components.lib.state.Middleware
import mozilla.components.lib.state.Store
import mozilla.telemetry.glean.GleanTimerId
import org.mozilla.fenix.GleanMetrics.AiSummarize

/**
 * Represents a full summarization session aggregation of telemetry data.
 *
 * @property sessionId A UUID identifying this summarization session, shared across every event recorded during the
 *   session.
 * @property trigger How the user initiated the summarization, or null until known.
 * @property model The identifier of the model used for summarization, or null until known.
 * @property startTimeMillis Wall-clock time the session started, used to compute durations.
 * @property contentMetrics Length/size metrics for the extracted content, or null until extracted.
 * @property receivedFirstChunk Whether the first response chunk has been received, used to record the first-response
 *   event only once.
 */
private data class SummarizationSessionTelemetry(
    val sessionId: String,
    val trigger: SummarizationTrigger? = null,
    val model: String? = null,
    val startTimeMillis: Long = System.currentTimeMillis(),
    val contentMetrics: ContentMetrics? = null,
    val receivedFirstChunk: Boolean = false,
)

/** Metrics representing the length/size of the content. */
private data class ContentMetrics(
    val wordCount: Int,
    val charCount: Int,
    val contentType: String? = null,
    val language: String,
)

/** Defines how the user initiated the summarization. */
private enum class SummarizationTrigger {
    SHAKE,
    MENU,
}

/** The type of network connection available on the device. */
enum class ConnectionType {
    WIFI,
    CELLULAR,
    OTHER,
    NONE,
}

/**
 * [Middleware] that records summarization telemetry by observing [SummarizationAction]s as they flow through the store,
 * aggregating session data into a [SummarizationSessionTelemetry].
 *
 * @param connectionType current network [ConnectionType].
 * @param sessionId A UUID identifying this summarization session, shared across every event recorded during the
 *   session. Defaults to a randomly generated UUID.
 * @param currentTimeMillis provider for the current time in milliseconds, injectable for testing.
 */
class SummarizationTelemetryMiddleware(
    private val connectionType: ConnectionType,
    sessionId: String = UUID.randomUUID().toString(),
    private val currentTimeMillis: () -> Long = { System.currentTimeMillis() },
) : Middleware<SummarizationState, SummarizationAction> {

    private var sessionTelemetry = SummarizationSessionTelemetry(sessionId = sessionId)
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
            is LlmProviderAction.ProviderInitialized -> recordProviderInitialized()
            is LlmProviderAction.SignInRequired -> recordSummarizationCompleted(success = false, action.reason)
            is ReceivedParsedDocument -> handleReceivedParsedDocument()
            is SummarizationCompleted -> recordSummarizationCompleted()
            is SummarizationFailed -> recordSummarizationCompleted(success = false, action.exception)
            is ViewDismissed -> handleViewDismissed(stateBefore, action)

            is OnDeviceSummarizationShakeConsentAction.AllowClicked,
            is OffDeviceSummarizationShakeConsentAction.AllowClicked -> recordConsentDisplayed(agreed = true)

            is OnDeviceSummarizationShakeConsentAction.CancelClicked,
            is OffDeviceSummarizationShakeConsentAction.CancelClicked -> recordConsentDisplayed(agreed = false)

            DownloadConsentAction.AllowClicked,
            DownloadConsentAction.CancelClicked,
            DownloadConsentAction.LearnMoreClicked,
            DownloadErrorAction.CancelClicked,
            DownloadErrorAction.LearnMoreClicked,
            DownloadErrorAction.TryAgainClicked,
            DownloadInProgressAction.CancelClicked,
            ErrorAction.ErrorDismissed,
            ErrorAction.LearnMoreClicked,
            OffDeviceSummarizationShakeConsentAction.LearnMoreClicked,
            OnDeviceSummarizationShakeConsentAction.LearnMoreClicked,
            PageLoadStarted,
            PageLoadCompleted,
            SettingsBackClicked,
            SettingsClicked,
            is SettingsLoaded,
            ShakeConsentRequested,
            SignInSummarizationContentAction.DismissClicked,
            SignInSummarizationContentAction.LearnMoreClicked,
            SignInSummarizationContentAction.SignInClicked,
            is SummarizeSettingsActionWrapper -> {}
        }
    }

    private fun handleViewAppeared(stateBefore: SummarizationState) {
        if (stateBefore !is SummarizationState.Inert) {
            return
        }

        val trigger =
            if (stateBefore.initializedWithShake) {
                SummarizationTrigger.SHAKE
            } else {
                SummarizationTrigger.MENU
            }
        sessionTelemetry = sessionTelemetry.copy(trigger = trigger)

        AiSummarize.requested.record(
            AiSummarize.RequestedExtra(
                trigger = sessionTelemetry.trigger?.toString(),
                sessionId = sessionTelemetry.sessionId,
            )
        )
        timerId = AiSummarize.duration.start()
    }

    private fun handleViewDismissed(stateBefore: SummarizationState, action: ViewDismissed) {
        AiSummarize.closed.record(
            AiSummarize.ClosedExtra(
                model = sessionTelemetry.model,
                engineAvailable = action.isEngineAvailable,
                sessionId = sessionTelemetry.sessionId,
            )
        )

        if (
            stateBefore is SummarizationState.ShakeConsentRequired ||
                stateBefore is SummarizationState.ShakeConsentWithDownloadRequired
        ) {
            recordConsentDisplayed(agreed = false)
        }
    }

    private fun recordConsentDisplayed(agreed: Boolean) {
        AiSummarize.consentDisplayed.record(
            AiSummarize.ConsentDisplayedExtra(
                agreed = agreed,
                sessionId = sessionTelemetry.sessionId,
            )
        )
    }

    private fun handleExtractedContent(content: Content) {
        sessionTelemetry =
            sessionTelemetry.copy(
                contentMetrics =
                    ContentMetrics(
                        wordCount = content.metadata.wordCount,
                        charCount = content.body.length,
                        contentType = content.metadata.structuredDataTypes.toString(),
                        language = content.metadata.language,
                    )
            )
        AiSummarize.started.record(
            AiSummarize.StartedExtra(
                contentType = sessionTelemetry.contentMetrics?.contentType,
                lengthChars = sessionTelemetry.contentMetrics?.charCount,
                lengthWords = sessionTelemetry.contentMetrics?.wordCount,
                model = sessionTelemetry.model,
                trigger = sessionTelemetry.trigger?.toString(),
                sessionId = sessionTelemetry.sessionId,
            )
        )
    }

    private fun recordProviderInitialized() {
        AiSummarize.providerInitialized.record(
            AiSummarize.ProviderInitializedExtra(
                model = sessionTelemetry.model,
                sessionId = sessionTelemetry.sessionId,
            )
        )
    }

    private fun handleReceivedParsedDocument() {
        if (sessionTelemetry.receivedFirstChunk) {
            return
        }
        sessionTelemetry = sessionTelemetry.copy(receivedFirstChunk = true)

        AiSummarize.firstResponse.record(
            AiSummarize.FirstResponseExtra(
                model = sessionTelemetry.model,
                sessionId = sessionTelemetry.sessionId,
            )
        )
    }

    /**
     * Identifier for the failure in telemetry. For [Llm.Exception] subtypes we log the qualified class name so provider
     * attribution survives (e.g. MLPA's `RateLimited` vs a hypothetical second provider's `RateLimited`). Bare
     * [Llm.Exception] instances and raw throwables fall back to the underlying cause's simple name, which is more
     * diagnostic than the generic wrapper class.
     */
    private fun Throwable.errorType(): String? =
        when {
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
                sessionId = sessionTelemetry.sessionId,
                success = success,
                summarizeDurationMs = (currentTimeMillis() - sessionTelemetry.startTimeMillis).toInt(),
            )
        )
    }
}
