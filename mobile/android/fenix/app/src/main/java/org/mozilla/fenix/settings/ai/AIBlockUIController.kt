/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.settings.ai

import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.launch
import mozilla.components.concept.ai.controls.AIFeatureBlock

internal data class AIBlockUIController(
    private val onBlockDialog: (Boolean) -> Unit,
) {

    private val _showDialogFlow: MutableStateFlow<Boolean> = MutableStateFlow(false)
    internal val showDialogFlow: StateFlow<Boolean> = _showDialogFlow

    internal fun onDialogDismiss() {
        _showDialogFlow.update { false }
    }

    internal fun onDialogConfirm() {
        _showDialogFlow.update { false }
        onBlockDialog(true)
    }

    internal fun onToggle(currentlyBlocked: Boolean) {
        if (currentlyBlocked) {
            onBlockDialog(false)
        } else {
            _showDialogFlow.update { true }
        }
    }

    companion object {
        internal fun default(
            featureBlock: AIFeatureBlock,
            scope: CoroutineScope,
        ) = AIBlockUIController(
            onBlockDialog = { blocked: Boolean ->
                scope.launch {
                    if (blocked) {
                        featureBlock.block()
                    } else {
                        featureBlock.unblock()
                    }
                }
            },
        )
    }
}
