/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.ask.page

import androidx.compose.animation.core.StartOffset
import androidx.compose.animation.core.animateFloat
import androidx.compose.animation.core.infiniteRepeatable
import androidx.compose.animation.core.keyframes
import androidx.compose.animation.core.rememberInfiniteTransition
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.lazy.rememberLazyListState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Button
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.alpha
import androidx.compose.ui.unit.dp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import mozilla.components.compose.base.theme.AcornTheme
import mozilla.components.concept.llm.Llm
import mozilla.components.ui.richtext.RichText
import mozilla.components.ui.richtext.ir.RichDocument
import mozilla.components.ui.richtext.parsing.Parser

/**
 * Top-level entry point for the ask-page conversational UI.
 *
 * @param store The [AskPageStore] driving the UI.
 */
@Composable
fun AskPageUi(store: AskPageStore) {
    LaunchedEffect(Unit) { store.dispatch(ViewAppeared) }

    val state by store.stateFlow.collectAsStateWithLifecycle()

    val messages = when (val s = state) {
        is AskPageState.Ready -> s.messages
        is AskPageState.Waiting -> s.messages
        is AskPageState.Receiving -> s.messages
        else -> emptyList()
    }

    AskPageScreen(
        messages = messages,
        pendingResponse = (state as? AskPageState.Receiving)?.pendingResponse,
        showLoadingBubble = state is AskPageState.Waiting,
        error = (state as? AskPageState.Ready)?.error,
        sendEnabled = state is AskPageState.Ready,
        onSubmit = { submittedText ->
            store.dispatch(UserMessageSubmitted(submittedText))
        },
    )
}

@Composable
internal fun AskPageScreen(
    messages: List<Llm.Message>,
    pendingResponse: PartialResponse?,
    showLoadingBubble: Boolean,
    error: Throwable?,
    sendEnabled: Boolean,
    onSubmit: (String) -> Unit,
) {
    val listState = rememberLazyListState()
    val itemCount = messages.size + if (showLoadingBubble || pendingResponse != null || error != null) 1 else 0
    LaunchedEffect(itemCount) {
        if (itemCount > 0) listState.scrollToItem(itemCount - 1)
    }

    Surface(modifier = Modifier.fillMaxHeight()) {
        Column(
            modifier = Modifier
                .fillMaxSize()
                .padding(horizontal = AcornTheme.layout.space.dynamic200),
        ) {
            LazyColumn(
                state = listState,
                modifier = Modifier
                    .fillMaxWidth()
                    .weight(1f),
                verticalArrangement = Arrangement.spacedBy(AcornTheme.layout.space.dynamic100),
            ) {
                items(messages) { message ->
                    MessageBubble(message)
                }
                when {
                    showLoadingBubble -> item { LoadingBubble() }
                    pendingResponse != null -> item { AssistantResponseBubble(pendingResponse.richDocument) }
                    error != null -> item { ErrorBubble(error) }
                }
            }

            UserPromptChip(
                onSubmit = onSubmit,
                enabled = sendEnabled,
                modifier = Modifier
                    .padding(
                        top = AcornTheme.layout.space.dynamic100,
                        bottom = AcornTheme.layout.space.dynamic200,
                    ),
            )
        }
    }
}

@Composable
private fun MessageBubble(message: Llm.Message) {
    val isUser = message is Llm.Message.User
    Row(
        modifier = Modifier.fillMaxWidth(),
        horizontalArrangement = if (isUser) Arrangement.End else Arrangement.Start,
    ) {
        Surface(
            shape = RoundedCornerShape(AcornTheme.layout.space.dynamic150),
            color = if (isUser) {
                MaterialTheme.colorScheme.primaryContainer
            } else {
                MaterialTheme.colorScheme.surfaceVariant
            },
        ) {
            val contentModifier = Modifier.padding(
                horizontal = AcornTheme.layout.space.dynamic150,
                vertical = AcornTheme.layout.space.dynamic100,
            )
            if (isUser) {
                Text(
                    text = message.message,
                    modifier = contentModifier,
                    style = MaterialTheme.typography.bodyMedium,
                )
            } else {
                val document = remember(message.message) { Parser().parse(message.message) }
                RichText(document = document, modifier = contentModifier)
            }
        }
    }
}

@Composable
private fun AssistantResponseBubble(document: RichDocument) {
    Row(
        modifier = Modifier.fillMaxWidth(),
        horizontalArrangement = Arrangement.Start,
    ) {
        Surface(
            shape = RoundedCornerShape(AcornTheme.layout.space.dynamic150),
            color = MaterialTheme.colorScheme.surfaceVariant,
        ) {
            RichText(
                document = document,
                modifier = Modifier.padding(
                    horizontal = AcornTheme.layout.space.dynamic150,
                    vertical = AcornTheme.layout.space.dynamic100,
                ),
            )
        }
    }
}

@Composable
private fun LoadingBubble() {
    Row(
        modifier = Modifier.fillMaxWidth(),
        horizontalArrangement = Arrangement.Start,
    ) {
        Surface(
            shape = RoundedCornerShape(AcornTheme.layout.space.dynamic150),
            color = MaterialTheme.colorScheme.surfaceVariant,
        ) {
            Row(
                modifier = Modifier.padding(
                    horizontal = AcornTheme.layout.space.dynamic150,
                    vertical = AcornTheme.layout.space.dynamic100,
                ),
                horizontalArrangement = Arrangement.spacedBy(4.dp),
                verticalAlignment = Alignment.CenterVertically,
            ) {
                val transition = rememberInfiniteTransition(label = "loading_dots")
                repeat(3) { i ->
                    val alpha by transition.animateFloat(
                        initialValue = 0.3f,
                        targetValue = 1f,
                        animationSpec = infiniteRepeatable(
                            animation = keyframes {
                                durationMillis = 1200
                                1f at 400
                                0.3f at 800
                            },
                            initialStartOffset = StartOffset(i * 200),
                        ),
                        label = "dot_alpha_$i",
                    )
                    Box(
                        modifier = Modifier
                            .size(8.dp)
                            .alpha(alpha)
                            .background(
                                color = MaterialTheme.colorScheme.onSurfaceVariant,
                                shape = CircleShape,
                            ),
                    )
                }
            }
        }
    }
}

@Composable
private fun ErrorBubble(error: Throwable) {
    Row(
        modifier = Modifier.fillMaxWidth(),
        horizontalArrangement = Arrangement.Start,
    ) {
        Surface(
            shape = RoundedCornerShape(AcornTheme.layout.space.dynamic150),
            color = MaterialTheme.colorScheme.errorContainer,
        ) {
            Text(
                text = "Something went wrong (${error::class.simpleName}). Please try again.",
                modifier = Modifier.padding(
                    horizontal = AcornTheme.layout.space.dynamic150,
                    vertical = AcornTheme.layout.space.dynamic100,
                ),
                style = MaterialTheme.typography.bodyMedium,
                color = MaterialTheme.colorScheme.onErrorContainer,
            )
        }
    }
}

@Composable
private fun UserPromptChip(
    onSubmit: (String) -> Unit,
    enabled: Boolean,
    modifier: Modifier = Modifier,
) {
    var text by remember { mutableStateOf("") }

    Row(
        modifier = modifier.fillMaxWidth(),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(AcornTheme.layout.space.dynamic100),
    ) {
        OutlinedTextField(
            value = text,
            onValueChange = { text = it },
            modifier = Modifier.weight(1f),
            shape = RoundedCornerShape(AcornTheme.layout.size.dynamic300),
            placeholder = { Text("Ask a question...") },
            singleLine = true,
        )
        Button(
            onClick = {
                onSubmit(text)
                text = ""
            },
            enabled = enabled,
        ) {
            Text("Send")
        }
    }
}
