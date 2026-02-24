/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.concept.llm

import kotlinx.coroutines.flow.StateFlow

/**
 * Marker interface representing a source of an [Llm].
 *
 * This is required as an underlying LLM model might have required setup steps. For example: Google
 * gemini nano needs to be downloaded before the user is able to submit it a prompt.
 *
 * Implementations may provide models from different environments
 * (e.g., cloud-hosted or locally installed).
 */
sealed interface LlmProvider

/**
 * A provider that exposes an LLM hosted in the cloud.
 *
 * Implementations are responsible for tracking availability and readiness
 * of the remote model and exposing that state via [state].
 */
interface CloudLlmProvider : LlmProvider {
    /**
     * Represents the current lifecycle state of a cloud LLM.
     */
    sealed interface State {

        /**
         * Indicates that the cloud provider is reachable and the model
         * can potentially be prepared for use.
         */
        object Available : State

        /**
         * Indicates that the cloud provider is unavailable.
         */
        object Unavailable : State

        /**
         * Indicates that the cloud LLM is fully initialized and ready for use.
         *
         * @property llm The ready-to-use LLM instance.
         */
        @JvmInline
        value class Ready(val llm: Llm) : State
    }

    /**
     * The current state of the [CloudLlmProvider]
     */
    val state: StateFlow<State>

    /** prepare the [CloudLlmProvider] */
    suspend fun prepare()
}

/**
 * A provider that exposes an LLM running locally on the device.
 *
 * Implementations are responsible for handling model availability,
 * download lifecycle, and readiness state.
 */
interface LocalLlmProvider : LlmProvider {
    /**
     * Represents the current lifecycle state of a local LLM.
     */
    sealed interface State {

        /**
         * Indicates that the provider is idle and no model is currently
         * being downloaded or prepared.
         */
        object Idle : State

        /**
         * Indicates that the local model cannot be used
         * (e.g., unsupported device, missing requirements).
         */
        object Unavailable : State

        /**
         * Indicates that the model is not yet present locally and must be downloaded.
         */
        object ReadyToDownload : State

        /**
         * Indicates that the model is currently downloading.
         *
         * @property bytesToDownload The number of bytes required to download.
         * @property bytesDownloaded The number of bytes downloaded so far.
         */
        data class Downloading(val bytesToDownload: Long, val bytesDownloaded: Long) : State

        /**
         * Indicates that the model provider is in a failed state.
         */
        object Failed : State

        /**
         * Indicates that the local LLM is fully initialized and ready for use.
         *
         * @property llm The ready-to-use LLM instance.
         */
        @JvmInline
        value class Ready(val llm: Llm) : State
    }

    /**
     * The current state of the [LocalLlmProvider]
     */
    val state: StateFlow<State>

    /**
     * Initiates a model download if required.
     *
     * If the model is already available locally, this method should return
     * immediately without side effects.
     */
    suspend fun downloadIfNeeded()
}

/**
 * Strategy interface used to select a single [LlmProvider] from a list
 * of available providers.
 *
 * Implementations may apply custom prioritization logic, such as:
 * - Preferring local providers over cloud
 * - Selecting only ready providers
 * - Falling back based on availability
 */
fun interface LlmPicker {
    /**
     * Selects one [LlmProvider] from the given list.
     *
     * @param llmProviders The available providers to choose from.
     * @return The selected provider.
     */
    fun pick(llmProviders: List<LlmProvider>): LlmProvider
}
