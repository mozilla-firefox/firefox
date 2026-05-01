/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.ask.page

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.lazy.rememberLazyListState
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
        is AskPageState.Receiving -> s.messages
        else -> emptyList()
    }

    AskPageScreen(
        messages = messages,
        pendingResponse = (state as? AskPageState.Receiving)?.pendingResponse,
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
    sendEnabled: Boolean,
    onSubmit: (String) -> Unit,
) {
    val listState = rememberLazyListState()
    val itemCount = messages.size + if (pendingResponse != null) 1 else 0
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
                if (pendingResponse != null) {
                    item {
                        AssistantResponseBubble(pendingResponse.richDocument)
                    }
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
