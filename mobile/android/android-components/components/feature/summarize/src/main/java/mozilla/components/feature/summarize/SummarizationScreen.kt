/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.summarize

import android.os.Build
import androidx.compose.animation.core.AnimationSpec
import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.animation.core.snap
import androidx.compose.animation.core.tween
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.WindowInsetsSides
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.only
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.requiredHeight
import androidx.compose.foundation.layout.requiredSize
import androidx.compose.foundation.layout.systemBars
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.layout.windowInsetsPadding
import androidx.compose.foundation.layout.wrapContentHeight
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CornerSize
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.runtime.Composable
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.compositionLocalOf
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.hapticfeedback.HapticFeedbackType
import androidx.compose.ui.input.nestedscroll.nestedScroll
import androidx.compose.ui.platform.LocalHapticFeedback
import androidx.compose.ui.platform.rememberNestedScrollInteropConnection
import androidx.compose.ui.tooling.preview.PreviewParameter
import androidx.compose.ui.tooling.preview.PreviewParameterProvider
import androidx.compose.ui.unit.dp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import mozilla.components.compose.base.annotation.FlexibleWindowLightDarkPreview
import mozilla.components.compose.base.modifier.thenConditional
import mozilla.components.compose.base.theme.AcornTheme
import mozilla.components.concept.llm.AttestationFailure
import mozilla.components.concept.llm.LlmProvider
import mozilla.components.concept.llm.RequestTooLarge
import mozilla.components.feature.summarize.settings.SettingsAppBar
import mozilla.components.feature.summarize.settings.SummarizeSettingsContent
import mozilla.components.feature.summarize.settings.SummarizeSettingsState
import mozilla.components.feature.summarize.ui.ContentTooLongError
import mozilla.components.feature.summarize.ui.DownloadError
import mozilla.components.feature.summarize.ui.FxaSignInContent
import mozilla.components.feature.summarize.ui.InfoError
import mozilla.components.feature.summarize.ui.OffDeviceSummarizationConsent
import mozilla.components.feature.summarize.ui.OnDeviceSummarizationConsent
import mozilla.components.feature.summarize.ui.SummarizingContent
import mozilla.components.feature.summarize.ui.SummaryContentLoaded
import mozilla.components.feature.summarize.ui.gradient.summaryLoadingGradient
import mozilla.components.ui.richtext.ir.RichDocument
import mozilla.components.ui.richtext.parsing.Parser

/** The corner ration of the handle shape */
private const val DRAG_HANDLE_CORNER_RATIO = 50

/**
 * Composable function that renders the summarized text of a webpage.
 *
 * @param resolveError Resolves a thrown failure into a numeric code for display (long-press reveal on the error icon,
 *   telemetry, support). The summarize feature has no knowledge of which concrete
 *   [mozilla.components.concept.llm.Llm.Exception] subtypes exist; the caller (app layer) supplies that mapping.
 */
@Composable
fun SummarizationUi(
    productName: String,
    store: SummarizationStore,
    resolveError: (Throwable) -> Int,
) {
    LaunchedEffect(Unit) {
        store.dispatch(ViewAppeared)
    }

    CompositionLocalProvider(LocalProductName provides ProductName(productName)) {
        SummarizationScreen(
            modifier = Modifier.fillMaxWidth(),
            store = store,
            errorCodeFor = resolveError,
        )
    }
}

@JvmInline internal value class ProductName(val value: String)

internal val LocalProductName = compositionLocalOf { ProductName("Firefox Debug") }

@Composable
private fun SummarizationScreen(
    modifier: Modifier = Modifier,
    store: SummarizationStore,
    errorCodeFor: (Throwable) -> Int,
) {
    val state by store.stateFlow.collectAsStateWithLifecycle()

    ApplyHaptics(state)

    val loadingAlpha by
        animateFloatAsState(
            targetValue =
                if ((state.isLoading || state.isPageLoading) && useGradient) {
                    1f
                } else {
                    0f
                },
            animationSpec =
                if (state.isLoading || state.isPageLoading) {
                    state.tween
                } else {
                    snap()
                },
            label = "gradientAlpha",
        )

    SummarizationScreenScaffold(
        modifier =
            modifier
                .summaryLoadingGradientCompat(loadingAlpha)
                .windowInsetsPadding(WindowInsets.systemBars.only(WindowInsetsSides.Bottom))
                .nestedScroll(rememberNestedScrollInteropConnection()),
        color = MaterialTheme.colorScheme.surface.copy(alpha = 1f - loadingAlpha),
    ) {
        SummarizationScreenContent(store, errorCodeFor)
    }
}

private val useGradient
    get() = Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q

private fun Modifier.summaryLoadingGradientCompat(loadingAlpha: Float): Modifier =
    this.thenConditional(
        if (useGradient) {
            Modifier.summaryLoadingGradient(loadingAlpha)
        } else {
            Modifier
        }
    ) {
        loadingAlpha > 0
    }

@Composable
private fun SummarizationScreenContent(
    store: SummarizationStore,
    errorCodeFor: (Throwable) -> Int,
) {
    val state by store.stateFlow.collectAsStateWithLifecycle()

    when (val state = state) {
        is SummarizationState.Inert -> Unit

        is SummarizationState.LearnMoreAboutShakeConsent,
        is SummarizationState.ShakeConsentRequired -> {
            OffDeviceSummarizationConsent(
                dispatchAction = {
                    store.dispatch(it)
                }
            )
        }

        is SummarizationState.ShakeConsentWithDownloadRequired -> {
            OnDeviceSummarizationConsent(
                dispatchAction = {
                    store.dispatch(it)
                }
            )
        }

        is SummarizationState.Loading,
        SummarizationState.PageLoading -> {
            SummarizingContent(
                modifier = Modifier.height(252.dp),
                useGradientColors = useGradient,
            )
        }

        is SummarizationState.Summarizing ->
            SummaryContentLoaded(
                info = state.info,
                document = state.document,
                onSettingsClicked = { store.dispatch(SettingsClicked) },
            )

        is SummarizationState.Summarized ->
            SummaryContentLoaded(
                info = state.info,
                document = state.document,
                onSettingsClicked = { store.dispatch(SettingsClicked) },
            )

        is SummarizationState.Settings -> {
            SettingsAppBar(onBackClicked = { store.dispatch(SettingsBackClicked) })

            SummarizeSettingsContent(
                state = state.settingsState,
                dispatch = { store.dispatch(SummarizeSettingsActionWrapper(it)) },
            )
        }

        is SummarizationState.SignInRequired -> {
            FxaSignInContent(dispatchAction = { store.dispatch(it) })
        }

        is SummarizationState.Error ->
            SummarizationErrorContent(
                error = state.error,
                errorCodeFor = errorCodeFor,
                dispatch = { store.dispatch(it) },
            )

        SummarizationState.DownloadConsentRequired,
        is SummarizationState.Downloading,
        SummarizationState.Finished.Cancelled,
        SummarizationState.Finished.ErrorDismissed,
        SummarizationState.Finished.NavigatedToSignIn,
        SummarizationState.LearnMoreAboutCloudSupportedFeatures -> Unit
    }
}

@Composable
private fun SummarizationErrorContent(
    error: SummarizationError,
    errorCodeFor: (Throwable) -> Int,
    dispatch: (SummarizationAction) -> Unit,
) {
    when (error) {
        is SummarizationError.DownloadFailed -> DownloadError()
        is SummarizationError.SummarizationFailed ->
            when (error.exception) {
                is RequestTooLarge -> ContentTooLongError(onDismiss = { dispatch(ErrorAction.ErrorDismissed) })
                is AttestationFailure -> {
                    FxaSignInContent(dispatchAction = { dispatch(it) })
                }
                else ->
                    InfoError(
                        errorCode = errorCodeFor(error.exception),
                        onDismiss = { dispatch(ErrorAction.ErrorDismissed) },
                    )
            }
    }
}

@Composable
private fun ApplyHaptics(state: SummarizationState) {
    val haptic = LocalHapticFeedback.current
    LaunchedEffect(state) {
        when (state) {
            is SummarizationState.Inert -> {
                if (state.initializedWithShake) {
                    haptic.performHapticFeedback(HapticFeedbackType.ToggleOn)
                }
            }
            is SummarizationState.Summarized -> {
                haptic.performHapticFeedback(HapticFeedbackType.Confirm)
            }
            is SummarizationState.Error -> {
                haptic.performHapticFeedback(HapticFeedbackType.Reject)
            }

            SummarizationState.DownloadConsentRequired,
            is SummarizationState.Downloading,
            is SummarizationState.Finished,
            SummarizationState.LearnMoreAboutShakeConsent,
            is SummarizationState.Loading,
            SummarizationState.PageLoading,
            is SummarizationState.Settings,
            SummarizationState.ShakeConsentRequired,
            SummarizationState.ShakeConsentWithDownloadRequired,
            is SummarizationState.Summarizing,
            SummarizationState.LearnMoreAboutCloudSupportedFeatures,
            SummarizationState.SignInRequired -> {}
        }
    }
}

@Composable
private fun SummarizationScreenScaffold(
    modifier: Modifier,
    color: Color = Color.Transparent,
    content: @Composable (() -> Unit),
) {
    Surface(
        shape =
            MaterialTheme.shapes.extraLarge.copy(
                bottomStart = CornerSize(0.dp),
                bottomEnd = CornerSize(0.dp),
            ),
        color = color,
        contentColor = MaterialTheme.colorScheme.onSurface,
        modifier =
            Modifier.fillMaxWidth()
                .wrapContentHeight(align = Alignment.Bottom)
                .clip(
                    MaterialTheme.shapes.extraLarge.copy(
                        bottomStart = CornerSize(0.dp),
                        bottomEnd = CornerSize(0.dp),
                    )
                )
                .then(modifier)
                .widthIn(max = AcornTheme.layout.size.containerMaxWidth),
    ) {
        Column(
            modifier =
                Modifier.fillMaxWidth().wrapContentHeight().padding(horizontal = AcornTheme.layout.space.static200)
        ) {
            DragHandle(modifier = Modifier.fillMaxWidth())
            Spacer(Modifier.height(AcornTheme.layout.space.static200))
            content()
            Spacer(Modifier.height(AcornTheme.layout.space.static400))
        }
    }
}

@Composable
private fun DragHandle(modifier: Modifier = Modifier) {
    Box(
        modifier = modifier.requiredHeight(36.dp).verticalScroll(rememberScrollState()),
        contentAlignment = Alignment.Center,
    ) {
        Box(
            modifier =
                Modifier.requiredSize(width = 32.dp, height = 4.dp)
                    .background(
                        color = MaterialTheme.colorScheme.outline,
                        shape = RoundedCornerShape(DRAG_HANDLE_CORNER_RATIO),
                    )
        )
    }
}

private val previewSummarizedText =
    """
    **What's happening:** Lucasfilm has announced a new Star Wars trilogy set in the Old Republic era, thousands of years before the Skywalker saga.

    **Why it matters:** Fans have long requested stories from this period, which explores the ancient conflict between the Jedi and the Sith at the height of their power.

    **Key details:**
    - Setting: The Old Republic, approximately 4,000 years before the Empire
    - Director: Yet to be announced
    - First film expected: 2028
    - Will feature the original Sith Wars and the founding of the Jedi Order
    """
        .trimIndent()

private class SummarizationStatePreviewProvider : PreviewParameterProvider<SummarizationState> {
    val info = LlmProvider.Info(R.string.mozac_summarize_fake_llm_name)
    val parser = Parser()
    override val values: Sequence<SummarizationState> =
        sequenceOf(
            SummarizationState.Summarizing(info = info),
            SummarizationState.Summarized(info = info, document = parser.parse(previewSummarizedText)),
            SummarizationState.Settings(
                info = info,
                document = RichDocument(listOf()),
                settingsState = SummarizeSettingsState(isFeatureEnabled = true, isGestureEnabled = true),
            ),
            SummarizationState.ShakeConsentRequired,
            SummarizationState.ShakeConsentWithDownloadRequired,
            SummarizationState.Error(SummarizationError.SummarizationFailed(NullPointerException("preview failure"))),
        )
}

@FlexibleWindowLightDarkPreview
@Composable
private fun SummarizationScreenPreview(
    @PreviewParameter(SummarizationStatePreviewProvider::class) state: SummarizationState
) {
    AcornTheme {
        SummarizationScreen(
            store =
                SummarizationStore(
                    initialState = state,
                    reducer = ::summarizationReducer,
                    middleware = listOf(),
                ),
            errorCodeFor = { 9999 },
        )
    }
}

private val SummarizationState.tween: AnimationSpec<Float>
    get() =
        if (isSummarizing) {
            tween(durationMillis = 3000)
        } else {
            snap()
        }
