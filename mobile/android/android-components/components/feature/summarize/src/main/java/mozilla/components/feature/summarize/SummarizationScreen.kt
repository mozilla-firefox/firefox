/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.summarize

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.requiredHeight
import androidx.compose.foundation.layout.requiredSize
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.runtime.Composable
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.runtime.compositionLocalOf
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.tooling.preview.Preview
import androidx.compose.ui.tooling.preview.PreviewParameter
import androidx.compose.ui.tooling.preview.PreviewParameterProvider
import androidx.compose.ui.unit.dp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import mozilla.components.compose.base.theme.AcornTheme
import mozilla.components.feature.summarize.ui.DownloadConsent
import mozilla.components.feature.summarize.ui.DownloadError
import mozilla.components.feature.summarize.ui.DownloadProgress
import mozilla.components.feature.summarize.ui.InfoError
import mozilla.components.feature.summarize.ui.OffDeviceSummarizationConsent
import mozilla.components.feature.summarize.ui.OnDeviceSummarizationConsent
import mozilla.components.lib.state.helpers.StoreProvider.Companion.composableStore

/**
 * The corner ration of the handle shape
 */
private const val DRAG_HANDLE_CORNER_RATIO = 50

/**
 * Composable function that renders the summarized text of a webpage.
 **/
@Composable
fun SummarizationUi(
    productName: String,
) {
    val store by composableStore(SummarizationState.initial) { state ->
        SummarizationStore(
            initialState = state,
            reducer = ::summarizationReducer,
            middleware = listOf(SummarizationMiddleware()),
        )
    }

    CompositionLocalProvider(LocalProductName provides ProductName(productName)) {
        SummarizationScreen(
            modifier = Modifier.fillMaxWidth(),
            store = store,
        )
    }
}

@JvmInline
internal value class ProductName(val value: String)
internal val LocalProductName = compositionLocalOf { ProductName("Firefox Debug") }

@Composable
private fun SummarizationScreen(
    modifier: Modifier = Modifier,
    store: SummarizationStore,
) {
    val state by store.stateFlow.collectAsStateWithLifecycle()

    SummarizationScreenScaffold(modifier = modifier) {
        when (val state = state) {
            is SummarizationState.Inert -> Unit
            is SummarizationState.ShakeConsentRequired,
            -> {
                OffDeviceSummarizationConsent(
                    dispatchAction = {
                        store.dispatch(it)
                    },
                )
            }
            is SummarizationState.ShakeConsentWithDownloadRequired -> {
                OnDeviceSummarizationConsent(
                    dispatchAction = {
                        store.dispatch(it)
                    },
                )
            }
            is SummarizationState.DownloadConsentRequired -> {
                DownloadConsent(
                    dispatchAction = {
                        store.dispatch(it)
                    },
                )
            }
            is SummarizationState.Downloading -> DownloadProgress(
                downloadState = state,
            )
            is SummarizationState.Error -> {
                if (state.error is SummarizationError.DownloadFailed) {
                    DownloadError()
                } else {
                    InfoError()
                }
            }

            else -> Unit
        }
    }
}

@Composable
private fun SummarizationScreenScaffold(
    modifier: Modifier,
    content: @Composable (() -> Unit),
) {
    Surface(
        shape = RoundedCornerShape(topStart = 24.dp, topEnd = 24.dp),
        color = MaterialTheme.colorScheme.surface,
        modifier = modifier
            .widthIn(max = AcornTheme.layout.size.containerMaxWidth)
            .fillMaxWidth(),
    ) {
        Column(
            modifier = Modifier
                .padding(horizontal = AcornTheme.layout.space.static200)
                .fillMaxWidth(),
        ) {
            DragHandle(modifier = Modifier.fillMaxWidth())
            Spacer(Modifier.height(AcornTheme.layout.space.static200))
            content()
            Spacer(Modifier.height(AcornTheme.layout.space.static400))
        }
    }
}

@Composable
private fun DragHandle(
    modifier: Modifier = Modifier,
) {
    Box(
        modifier = modifier.requiredHeight(36.dp),
        contentAlignment = Alignment.Center,
    ) {
        Box(
            modifier = Modifier
                .requiredSize(width = 32.dp, height = 4.dp)
                .background(
                    color = MaterialTheme.colorScheme.outline,
                    shape = RoundedCornerShape(DRAG_HANDLE_CORNER_RATIO),
                ),
        )
    }
}

private class SummarizationStatePreviewProvider : PreviewParameterProvider<SummarizationState> {
    override val values: Sequence<SummarizationState> = sequenceOf(
        SummarizationState.Summarizing,
        SummarizationState.Error(SummarizationError.ContentTooLong),
        SummarizationState.ShakeConsentRequired,
        SummarizationState.ShakeConsentWithDownloadRequired,
        SummarizationState.DownloadConsentRequired,
        SummarizationState.Downloading(12.13f, 9.04f),
        SummarizationState.Error(SummarizationError.NetworkError),
    )
}

@Preview
@Composable
private fun SummarizationScreenPreview(
    @PreviewParameter(SummarizationStatePreviewProvider::class) state: SummarizationState,
) {
    SummarizationScreen(
        store = SummarizationStore(
            initialState = state,
            reducer = ::summarizationReducer,
            middleware = listOf(SummarizationMiddleware()),
        ),
    )
}
