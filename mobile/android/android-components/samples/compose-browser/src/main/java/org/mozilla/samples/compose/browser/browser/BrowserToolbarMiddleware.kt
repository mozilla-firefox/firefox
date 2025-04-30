/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.samples.compose.browser.browser

import android.content.Context
import androidx.annotation.ColorRes
import androidx.core.content.ContextCompat
import androidx.navigation.NavController
import mozilla.components.compose.browser.toolbar.concept.Action.ActionButton
import mozilla.components.compose.browser.toolbar.concept.Action.TabCounterAction
import mozilla.components.compose.browser.toolbar.concept.PageOrigin
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarAction
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarAction.ToggleEditMode
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarInteraction.BrowserToolbarEvent
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarInteraction.BrowserToolbarMenu
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarMenuItem.BrowserToolbarMenuButton
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarState
import mozilla.components.compose.browser.toolbar.store.DisplayState
import mozilla.components.compose.browser.toolbar.store.EditState
import mozilla.components.compose.browser.toolbar.store.Mode
import mozilla.components.lib.state.Middleware
import mozilla.components.lib.state.MiddlewareContext
import mozilla.components.lib.state.Store
import org.mozilla.samples.compose.browser.BrowserComposeActivity.Companion.ROUTE_SETTINGS
import org.mozilla.samples.compose.browser.R
import org.mozilla.samples.compose.browser.browser.DisplayBrowserActionsInteractions.TabCounterClicked
import org.mozilla.samples.compose.browser.browser.DisplayPageActionsEndInteractions.RefreshClicked
import org.mozilla.samples.compose.browser.browser.DisplayPageOriginInteractions.PageOriginClicked
import org.mozilla.samples.compose.browser.browser.EditActionsInteractions.ClearClicked
import mozilla.components.ui.icons.R as iconsR

private sealed class DisplayPageOriginInteractions : BrowserToolbarEvent {
    data object PageOriginClicked : DisplayPageOriginInteractions()
}

private sealed class DisplayPageActionsEndInteractions : BrowserToolbarEvent {
    data object RefreshClicked : DisplayPageActionsEndInteractions()
}

private sealed class DisplayBrowserActionsInteractions : BrowserToolbarEvent {
    data object TabCounterClicked : DisplayBrowserActionsInteractions()
}

private sealed class MenuInteractions : BrowserToolbarEvent {
    data object SettingsClicked : MenuInteractions()
}

private sealed class EditActionsInteractions : BrowserToolbarEvent {
    data object ClearClicked : EditActionsInteractions()
}

internal class BrowserToolbarMiddleware(
    initialDependencies: Dependencies,
) : Middleware<BrowserToolbarState, BrowserToolbarAction> {
    var dependencies = initialDependencies

    override fun invoke(
        context: MiddlewareContext<BrowserToolbarState, BrowserToolbarAction>,
        next: (BrowserToolbarAction) -> Unit,
        action: BrowserToolbarAction,
    ) {
        when (action) {
            is BrowserToolbarAction.Init -> {
                next(buildInitialState())
            }

            is PageOriginClicked -> {
                next(ToggleEditMode(true))
            }

            is TabCounterClicked -> {
                dependencies.browserScreenStore.dispatch(BrowserScreenAction.ShowTabs)
            }

            is MenuInteractions.SettingsClicked -> {
                dependencies.navController.navigate(ROUTE_SETTINGS)
            }

            else -> {
                next(action)
            }
        }
    }

    private fun buildInitialState() = BrowserToolbarAction.Init(
        mode = Mode.DISPLAY,
        displayState = DisplayState(
            pageOrigin = PageOrigin(
                hint = R.string.toolbar_search_hint,
                title = null,
                url = null,
                onClick = PageOriginClicked,
            ),
            pageActionsEnd = buildDisplayPageActions(),
            browserActionsEnd = buildDisplayBrowserActions(),
        ),
        editState = EditState(
            editActionsEnd = buildEditPageActionsEnd(),
        ),
    )

    private fun buildDisplayPageActions() = listOf(
        ActionButton(
            icon = iconsR.drawable.mozac_ic_arrow_clockwise_24,
            contentDescription = R.string.page_action_refresh_description,
            tint = getColor(R.color.icon_tint),
            onClick = RefreshClicked,
        ),
    )

    private fun buildDisplayBrowserActions() = listOf(
        TabCounterAction(
            count = 1,
            contentDescription = "Tabs open: 1",
            showPrivacyMask = false,
            onClick = TabCounterClicked,
        ),

        ActionButton(
            icon = iconsR.drawable.mozac_ic_ellipsis_vertical_24,
            contentDescription = R.string.menu_button_description,
            tint = getColor(R.color.icon_tint),
            onClick = BrowserToolbarMenu {
                listOf(
                    BrowserToolbarMenuButton(
                        iconResource = iconsR.drawable.mozac_ic_settings_24,
                        text = R.string.menu_item_settings,
                        contentDescription = R.string.menu_item_settings_description,
                        onClick = MenuInteractions.SettingsClicked,
                    ),
                )
            },
        ),
    )

    private fun buildEditPageActionsEnd() = listOf(
        ActionButton(
            icon = iconsR.drawable.mozac_ic_stop,
            contentDescription = R.string.clear_input_description,
            tint = getColor(R.color.icon_tint),
            onClick = ClearClicked,
        ),
    )

    private fun getColor(@ColorRes id: Int) = ContextCompat.getColor(dependencies.context, id)

    companion object {
        data class Dependencies(
            val context: Context,
            val navController: NavController,
            val browserScreenStore: Store<BrowserScreenState, BrowserScreenAction>,
        )
    }
}
