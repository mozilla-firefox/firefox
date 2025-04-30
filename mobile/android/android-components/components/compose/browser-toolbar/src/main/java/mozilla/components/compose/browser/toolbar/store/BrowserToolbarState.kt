/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.compose.browser.toolbar.store

import androidx.annotation.IntRange
import androidx.compose.ui.graphics.Color
import mozilla.components.compose.browser.toolbar.R
import mozilla.components.compose.browser.toolbar.concept.Action
import mozilla.components.compose.browser.toolbar.concept.PageOrigin
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarInteraction.BrowserToolbarEvent
import mozilla.components.lib.state.State

/**
 * The state of the browser toolbar.
 *
 * @property mode The display [Mode] of the browser toolbar.
 * @property displayState Wrapper containing the toolbar display state.
 * @property editState Wrapper containing the toolbar edit state.
 */
data class BrowserToolbarState(
    val mode: Mode = Mode.DISPLAY,
    val displayState: DisplayState = DisplayState(),
    val editState: EditState = EditState(),
) : State {

    /**
     * Returns true if the browser toolbar is in edit mode and false otherwise.
     */
    fun isEditMode() = this.mode == Mode.EDIT
}

/**
 * The various display mode of the browser toolbar.
 */
enum class Mode {
    /**
     * Display mode - Shows the URL and related toolbar actions.
     */
    DISPLAY,

    /**
     * Edit mode - Allows the user to edit the URL.
     */
    EDIT,
}

/**
 * Wrapper containing the toolbar display state.
 *
 * @property browserActionsStart List of browser [Action]s to be displayed at the start of the
 * toolbar, outside of the URL bounding box.
 * These should be actions relevant to the browser as a whole.
 * See [MDN docs](https://developer.mozilla.org/en-US/docs/Mozilla/Add-ons/WebExtensions/API/browserAction).
 * @property pageActionsStart List of navigation [Action]s to be displayed between [browserActionsStart]
 * and the current webpage's details, inside of the URL bounding box.
 * These should be actions relevant to specific webpages as opposed to [browserActionsStart].
 * See [MDN docs](https://developer.mozilla.org/en-US/docs/Mozilla/Add-ons/WebExtensions/API/pageAction).
 * @property pageOrigin Details about the current website.
 * @property pageActionsEnd List of page [Action]s to be displayed between [pageOrigin] and [browserActionsEnd],
 * inside of the URL bounding box.
 * These should be actions relevant to specific webpages as opposed to [browserActionsStart].
 * See [MDN docs](https://developer.mozilla.org/en-US/docs/Mozilla/Add-ons/WebExtensions/API/pageAction).
 * @param browserActionsEnd List of browser [Action]s to be displayed at the end of the toolbar,
 * outside of the URL bounding box.
 * These should be actions relevant to the browser as a whole.
 * See [MDN docs](https://developer.mozilla.org/en-US/docs/Mozilla/Add-ons/WebExtensions/API/browserAction).
 * @property progressBarConfig [ProgressBarConfig] configuration for the progress bar.
 * If `null` a progress bar will not be displayed.
 */
data class DisplayState(
    val browserActionsStart: List<Action> = emptyList(),
    val pageActionsStart: List<Action> = emptyList(),
    val pageOrigin: PageOrigin = PageOrigin(
        hint = R.string.mozac_browser_toolbar_search_hint,
        title = null,
        url = null,
        onClick = object : BrowserToolbarEvent {},
    ),
    val pageActionsEnd: List<Action> = emptyList(),
    val browserActionsEnd: List<Action> = emptyList(),
    val progressBarConfig: ProgressBarConfig? = null,
) : State

/**
 * @property progress `[0 - 100]` progress to show.
 * @property gravity Top/bottom gravity of the progress bar.
 * @property color List of colors to use for the progress bar.
 * If more are provided then the progress bar will show them as a gradient.
 * If `null` is provided the default colors will be used.
 */
data class ProgressBarConfig(
    @IntRange(from = 0, to = 100) val progress: Int,
    val gravity: ProgressBarGravity,
    val color: List<Color>? = null,
)

/**
 * Where should the progress bar be shown in relation to the browser toolbar.
 */
sealed class ProgressBarGravity {
    /**
     * Show the progress bar at the top of the browser toolbar.
     */
    data object Top : ProgressBarGravity()

    /**
     * Show the progress bar at the bottom of the browser toolbar.
     */
    data object Bottom : ProgressBarGravity()
}

/**
 * Wrapper containing the toolbar edit state.
 *
 * @property editText The text the user is editing in "edit" mode.
 * @property editActionsStart List of [Action]s to be displayed at the start of the URL of
 * the edit toolbar.
 * @property editActionsEnd List of [Action]s to be displayed at the end of the URL of
 * the edit toolbar.
 */
data class EditState(
    val editText: String? = null,
    val editActionsStart: List<Action> = emptyList(),
    val editActionsEnd: List<Action> = emptyList(),
) : State
