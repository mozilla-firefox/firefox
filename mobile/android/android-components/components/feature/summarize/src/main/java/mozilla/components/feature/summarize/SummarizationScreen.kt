/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.summarize

import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.tooling.preview.Preview
import androidx.compose.ui.tooling.preview.PreviewParameter
import androidx.compose.ui.tooling.preview.PreviewParameterProvider
import androidx.compose.ui.unit.dp

/**
 * Composable function that renders the summarized text of a webpage.
 **/
@Composable
fun SummarizationUi() {
    SummarizationScreen(
        SummarizationStore(
            initialState = SummarizationState(
                pageSummarizationState = PageSummarizationState.Inert,
            ),
            reducer = ::summarizationReducer,
            middleware = listOf(SummarizationMiddleware()),
        ),
    )
}

@Composable
private fun SummarizationScreen(
    store: SummarizationStore,
) {

    Surface(
        shape = RoundedCornerShape(topStart = 24.dp, topEnd = 24.dp),
        color = MaterialTheme.colorScheme.surface,
        modifier = Modifier.fillMaxWidth(),
    ) {
        Column(
            modifier = Modifier
                .fillMaxWidth()
                .padding(horizontal = 24.dp, vertical = 16.dp),
        ) {
            Text(
                text = "Lorem Ipsum",
                style = MaterialTheme.typography.bodyMedium,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
                modifier = Modifier.padding(top = 8.dp),
            )
        }
    }
}

private class SummarizationStatePreviewProvider : PreviewParameterProvider<SummarizationState> {
    override val values: Sequence<SummarizationState> = sequenceOf(
        SummarizationState(
            PageSummarizationState.Summarizing,
            summarizedText = "Lorem Ipsum",
        ),
        SummarizationState(
            PageSummarizationState.Error(SummarizationError.ContentTooLong),
        ),
        SummarizationState(
            PageSummarizationState.WaitingForPermission,
        ),
    )
}

@Preview
@Composable
private fun SummarizationScreenPreview(
    @PreviewParameter(SummarizationStatePreviewProvider::class) state: SummarizationState,
) {
    SummarizationScreen(
        SummarizationStore(
            initialState = state,
            reducer = ::summarizationReducer,
            middleware = listOf(SummarizationMiddleware()),
        ),
    )
}
