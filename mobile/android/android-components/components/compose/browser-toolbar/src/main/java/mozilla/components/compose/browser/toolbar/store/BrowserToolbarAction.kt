/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.compose.browser.toolbar.store

import mozilla.components.compose.browser.toolbar.concept.PageOrigin
import mozilla.components.lib.state.Action
import mozilla.components.compose.browser.toolbar.concept.Action as ToolbarAction

/**
 * [Action]s for updating the [BrowserToolbarState] via [BrowserToolbarStore].
 */
sealed interface BrowserToolbarAction : Action {
    /**
     * Updates whether the toolbar is in "display" or "edit" mode.
     *
     * @property editMode Whether or not the toolbar is in "edit" mode.
     */
    data class ToggleEditMode(val editMode: Boolean) : BrowserToolbarAction

    /**
     * Initialize the toolbar with the provided data.
     *
     * @property mode The initial mode of the toolbar.
     * @property displayState The initial state of the display toolbar.
     * @property editState The initial state of the edit toolbar.
     */
    data class Init(
        val mode: Mode = Mode.DISPLAY,
        val displayState: DisplayState = DisplayState(),
        val editState: EditState = EditState(),
    ) : BrowserToolbarAction
}

/**
 * [BrowserToolbarAction] implementations related to updating the browser display toolbar.
 */
sealed class BrowserDisplayToolbarAction : BrowserToolbarAction {
    /**
     * Replaces the currently displayed list of start browser actions with the provided list of actions.
     * These are displayed to the left side of the URL, outside of the URL bounding box.
     *
     * @property actions The new list of [ToolbarAction]s.
     */
    data class BrowserActionsStartUpdated(val actions: List<ToolbarAction>) : BrowserDisplayToolbarAction()

    /**
     * Replaces the currently displayed list of start page actions with the provided list of actions.
     * These are displayed to the left side of the URL, inside of the URL bounding box.
     *
     * @property actions The new list of [ToolbarAction]s.
     */
    data class PageActionsStartUpdated(val actions: List<ToolbarAction>) : BrowserDisplayToolbarAction()

    /**
     * Replace the currently displayed details of the page with the newly provided details.
     *
     * @property pageOrigin The new details of the current webpage.
     */
    data class PageOriginUpdated(val pageOrigin: PageOrigin) : BrowserDisplayToolbarAction()

    /**
     * Replaces the currently displayed list of end page actions with the provided list of actions.
     * These are displayed to the right side of the URL, inside of the URL bounding box.
     *
     * @property actions The new list of [ToolbarAction]s.
     */
    data class PageActionsEndUpdated(val actions: List<ToolbarAction>) : BrowserDisplayToolbarAction()

    /**
     * Replaces the currently displayed list of end browser actions with the provided list of actions.
     * These are displayed to the right side of the URL, outside of the URL bounding box.
     *
     * @property actions The new list of [ToolbarAction]s.
     */
    data class BrowserActionsEndUpdated(val actions: List<ToolbarAction>) : BrowserDisplayToolbarAction()

    /**
     * Updates the [ProgressBarConfig] of the display toolbar.
     *
     * @property config The new configuration for what progress bar to show.
     */
    data class UpdateProgressBarConfig(val config: ProgressBarConfig?) : BrowserDisplayToolbarAction()
}

/**
 * [BrowserToolbarAction] implementations related to updating the browser edit toolbar.
 */
sealed class BrowserEditToolbarAction : BrowserToolbarAction {
    /**
     * Updates the text of the toolbar that is currently being edited (in "edit" mode).
     *
     * @property text The text in the toolbar that is being edited.
     */
    data class UpdateEditText(val text: String) : BrowserEditToolbarAction()

    /**
     * Adds an [Action] to be displayed at the start of the URL in the browser edit toolbar.
     *
     * @property action The [Action] to be added.
     */
    data class AddEditActionStart(val action: ToolbarAction) : BrowserEditToolbarAction()

    /**
     * Adds an [Action] to be displayed at the end of the URL in the browser edit toolbar.
     *
     * @property action The [Action] to be added.
     */
    data class AddEditActionEnd(val action: ToolbarAction) : BrowserEditToolbarAction()
}
