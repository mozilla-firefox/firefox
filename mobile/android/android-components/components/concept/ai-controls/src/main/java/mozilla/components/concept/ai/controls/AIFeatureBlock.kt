/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.concept.ai.controls

import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.MutableStateFlow

/**
 * Controls blocking and unblocking of all AI features.
 */
interface AIFeatureBlock {
    val isBlocked: Flow<Boolean>

    /**
     * Blocks all AI features.
     */
    suspend fun block()

    /**
     * Unblocks all AI features.
     */
    suspend fun unblock()

    companion object {
        /**
         * Creates a simple in-memory implementation of [AIFeatureBlock] for use in tests or previews.
         */
        fun inMemory(initialBlocked: Boolean = false): AIFeatureBlock = InMemoryAIFeatureBlock(initialBlocked)
    }
}

private class InMemoryAIFeatureBlock(initialBlocked: Boolean) : AIFeatureBlock {
    private val _isBlocked = MutableStateFlow(initialBlocked)
    override val isBlocked: Flow<Boolean> = _isBlocked

    override suspend fun block() {
        _isBlocked.value = true
    }

    override suspend fun unblock() {
        _isBlocked.value = false
    }
}
