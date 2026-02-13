/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.debugsettings.llm

import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import kotlinx.coroutines.launch
import mozilla.components.compose.base.button.FilledButton
import mozilla.components.concept.llm.CloudLlmProvider.State
import mozilla.components.concept.llm.Llm
import mozilla.components.concept.llm.Prompt
import mozilla.components.lib.llm.mlpa.MlpaLlmProvider
import org.mozilla.fenix.R
import org.mozilla.fenix.components.Llm as LlmComponent

/**
 * Debug drawer view to test an [IntegrityClient].
 *
 * @param llm Llm components.
 */
@Composable
fun LlmTools(
    llm: LlmComponent,
) {
    val state by llm.mlpaProvider.state.collectAsState()

    when (state) {
        State.Unavailable -> UnavailableState()
        is State.Available -> AvailableState(llm.mlpaProvider)
        is State.Ready -> ReadyState((state as State.Ready).llm)
    }
}

@Composable
private fun ReadyState(llm: Llm) {
    val scope = rememberCoroutineScope()

    var llmResponse by remember { mutableStateOf("") }

    Column {
        FilledButton(
            text = stringResource(R.string.llm_debug_tools_prompt_button),
            modifier = Modifier.fillMaxWidth(),
            onClick = {
                scope.launch {
                    llmResponse = ""
                    llm.prompt(Prompt("Hello World")).collect { response ->
                        when (response) {
                            is Llm.Response.Failure -> {
                                llmResponse = "There was a problem: ${response.reason}"
                            }
                            is Llm.Response.Success.ReplyPart -> llmResponse += response.value
                            else -> {}
                        }
                    }
                }
            },
        )

        Text(llmResponse)
    }
}

@Composable
private fun UnavailableState() {
    Text("LLM is unavailable.")
}

@Composable
private fun AvailableState(provider: MlpaLlmProvider) {
    val scope = rememberCoroutineScope()

    FilledButton(
        text = stringResource(R.string.llm_debug_tools_prepare_button),
        modifier = Modifier.fillMaxWidth(),
        onClick = {
            scope.launch { provider.prepare() }
        },
    )
}
