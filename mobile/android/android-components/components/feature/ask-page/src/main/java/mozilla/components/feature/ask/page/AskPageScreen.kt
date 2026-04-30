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

/**
 * Top-level entry point for the ask-page conversational UI.
 *
 * @param store The [AskPageStore] driving the UI.
 */
@Composable
fun AskPageUi(store: AskPageStore) {
    LaunchedEffect(Unit) { store.dispatch(ViewAppeared) }

    val state by store.stateFlow.collectAsStateWithLifecycle()
    val messages = (state as? AskPageState.Active)?.messages ?: emptyList()

    AskPageScreen(
        messages = messages,
        onSubmit = { submittedText ->
            store.dispatch(UserMessageSubmitted(submittedText))
        },
    )
}

@Composable
internal fun AskPageScreen(
    messages: List<Llm.Message>,
    onSubmit: (String) -> Unit,
) {
    Surface(modifier = Modifier.fillMaxHeight()) {
        Column(
            modifier = Modifier
                .fillMaxSize()
                .padding(horizontal = AcornTheme.layout.space.dynamic200),
        ) {
            LazyColumn(
                modifier = Modifier
                    .fillMaxWidth()
                    .weight(1f),
                verticalArrangement = Arrangement.spacedBy(AcornTheme.layout.space.dynamic100),
            ) {
                items(messages) { message ->
                    MessageBubble(message)
                }
            }

            UserPromptChip(
                onSubmit = onSubmit,
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
            Text(
                text = message.message,
                modifier = Modifier.padding(
                    horizontal = AcornTheme.layout.space.dynamic150,
                    vertical = AcornTheme.layout.space.dynamic100,
                ),
                style = MaterialTheme.typography.bodyMedium,
            )
        }
    }
}

@Composable
private fun UserPromptChip(
    onSubmit: (String) -> Unit,
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
        ) {
            Text("Send")
        }
    }
}
