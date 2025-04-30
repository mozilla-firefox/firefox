/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.home.setup.ui

import android.view.View
import androidx.compose.runtime.Composable
import androidx.compose.ui.platform.ComposeView
import androidx.lifecycle.LifecycleOwner
import mozilla.components.lib.state.ext.observeAsComposableState
import org.mozilla.fenix.components.components
import org.mozilla.fenix.compose.ComposeViewHolder
import org.mozilla.fenix.home.sessioncontrol.SetupChecklistInteractor

/**
 * View holder for the Setup Checklist feature.
 *
 * @param composeView [ComposeView] which will be populated with Jetpack Compose UI content.
 * @param viewLifecycleOwner [LifecycleOwner] to which this Composable will be tied to.
 * @param interactor [SetupChecklistInteractor] to handle user interactions.
 */
class SetupChecklistViewHolder(
    composeView: ComposeView,
    viewLifecycleOwner: LifecycleOwner,
    private val interactor: SetupChecklistInteractor,
) : ComposeViewHolder(composeView, viewLifecycleOwner) {

    @Composable
    override fun Content() {
        val appStore = components.appStore
        val setupChecklistState =
            appStore.observeAsComposableState { state -> state.setupChecklistState }.value

        if (setupChecklistState != null && setupChecklistState.isVisible) {
            SetupChecklist(
                setupChecklistState = setupChecklistState,
                interactor = interactor,
            )
        }
    }

    /**
     * The layout ID for this view holder.
     */
    companion object {
        val LAYOUT_ID = View.generateViewId()
    }
}
