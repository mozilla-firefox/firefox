/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.support.utils

import android.view.View
import android.view.ViewTreeObserver
import androidx.activity.ComponentActivity
import androidx.activity.compose.LocalActivity
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.State
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import mozilla.components.support.utils.ext.isKeyboardVisible

/** Detects if the keyboard is opened or closed and returns as a [KeyboardState]. */
@Composable
fun keyboardAsState(): State<KeyboardState> {
    val keyboardState = remember { mutableStateOf(KeyboardState.Closed) }

    val currentActivity = (LocalActivity.current as? ComponentActivity)
    DisposableEffect(currentActivity) {
        var layoutListener: ViewTreeObserver.OnGlobalLayoutListener?
        val contentView: View? = currentActivity?.findViewById(android.R.id.content)

        layoutListener = ViewTreeObserver.OnGlobalLayoutListener {
            keyboardState.value =
                if (contentView?.isKeyboardVisible() == true) {
                    KeyboardState.Opened
                } else {
                    KeyboardState.Closed
                }
        }
        contentView?.viewTreeObserver?.addOnGlobalLayoutListener(layoutListener)

        onDispose {
            contentView?.viewTreeObserver?.removeOnGlobalLayoutListener(layoutListener)
        }
    }

    return keyboardState
}

/** Represents the current state of the keyboard, opened or closed. */
enum class KeyboardState {
    Opened,
    Closed,
}
