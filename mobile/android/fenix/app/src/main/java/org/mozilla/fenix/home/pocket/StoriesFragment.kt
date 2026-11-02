/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.home.pocket

import android.os.Bundle
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.fragment.app.Fragment
import androidx.fragment.compose.content
import androidx.lifecycle.lifecycleScope
import androidx.navigation.fragment.findNavController
import java.lang.ref.WeakReference
import kotlinx.coroutines.flow.map
import org.mozilla.fenix.components.appstate.recommendations.ContentRecommendationsState
import org.mozilla.fenix.components.components
import org.mozilla.fenix.e2e.SystemInsetsPaddedFragment
import org.mozilla.fenix.ext.requireComponents
import org.mozilla.fenix.home.pocket.controller.DefaultPocketStoriesController
import org.mozilla.fenix.home.pocket.controller.PocketStoriesController
import org.mozilla.fenix.home.pocket.interactor.DefaultPocketStoriesInteractor
import org.mozilla.fenix.home.pocket.interactor.PocketStoriesInteractor
import org.mozilla.fenix.home.pocket.ui.StoriesScreen
import org.mozilla.fenix.theme.FirefoxTheme

/** A [Fragment] displaying the stories screen. */
class StoriesFragment : Fragment(), SystemInsetsPaddedFragment {

    private lateinit var interactor: PocketStoriesInteractor
    private lateinit var controller: PocketStoriesController

    override fun onViewCreated(view: View, savedInstanceState: Bundle?) {
        super.onViewCreated(view, savedInstanceState)

        controller =
            DefaultPocketStoriesController(
                navControllerRef = WeakReference(findNavController()),
                appStore = requireComponents.appStore,
                settings = requireComponents.settings,
                fenixBrowserUseCases = requireComponents.useCases.fenixBrowserUseCases,
                marsUseCases = requireComponents.useCases.marsUseCases,
                viewLifecycleScope = viewLifecycleOwner.lifecycleScope,
            )

        interactor = DefaultPocketStoriesInteractor(controller = controller)
    }

    override fun onCreateView(
        inflater: LayoutInflater,
        container: ViewGroup?,
        savedInstanceState: Bundle?,
    ): View? = content {
        FirefoxTheme {
            val appStore = components.appStore
            val storiesState by remember {
                appStore.stateFlow.map { state -> state.recommendationState }
            }
                .collectAsState(initial = ContentRecommendationsState())

            StoriesScreen(
                state = storiesState,
                interactor = interactor,
                onNavigationIconClick = {
                    this@StoriesFragment.findNavController().popBackStack()
                },
            )
        }
    }
}
