/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.home.topsites

import android.os.Bundle
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import androidx.fragment.app.Fragment
import androidx.fragment.compose.content
import androidx.lifecycle.lifecycleScope
import androidx.navigation.fragment.findNavController
import mozilla.components.feature.top.sites.presenter.DefaultTopSitesPresenter
import mozilla.components.lib.state.helpers.StoreProvider.Companion.fragmentStore
import mozilla.components.support.base.feature.ViewBoundFeatureWrapper
import org.mozilla.fenix.e2e.SystemInsetsPaddedFragment
import org.mozilla.fenix.ext.requireComponents
import org.mozilla.fenix.home.topsites.controller.DefaultTopSiteController
import org.mozilla.fenix.home.topsites.controller.TopSiteController
import org.mozilla.fenix.home.topsites.controller.TopSitesSource
import org.mozilla.fenix.home.topsites.interactor.DefaultTopSiteInteractor
import org.mozilla.fenix.home.topsites.interactor.TopSiteInteractor
import org.mozilla.fenix.home.topsites.middleware.ShortcutsMiddleware
import org.mozilla.fenix.home.topsites.store.ShortcutsState
import org.mozilla.fenix.home.topsites.store.ShortcutsStore
import org.mozilla.fenix.home.topsites.ui.ShortcutsScreen
import org.mozilla.fenix.theme.FirefoxTheme
import java.lang.ref.WeakReference

/**
 * A [Fragment] displaying the shortcuts screen.
 */
class ShortcutsFragment : Fragment(), SystemInsetsPaddedFragment {

    private val topSitesBinding = ViewBoundFeatureWrapper<TopSitesBinding>()

    private lateinit var interactor: TopSiteInteractor
    private lateinit var controller: TopSiteController

    private val shortcutsStore by fragmentStore(
        initialState = ShortcutsState.INITIAL,
    ) {
        ShortcutsStore(
            initialState = it,
            middleware = listOf(
                ShortcutsMiddleware(
                    appStore = requireComponents.appStore,
                    topSitesUseCases = requireComponents.useCases.topSitesUseCase,
                    merinoManifestProvider = requireComponents.core.merinoManifestProvider,
                    settings = requireComponents.settings,
                    scope = viewLifecycleOwner.lifecycleScope,
                ),
            ),
        )
    }

    override fun onViewCreated(view: View, savedInstanceState: Bundle?) {
        super.onViewCreated(view, savedInstanceState)

        controller = DefaultTopSiteController(
            activityRef = WeakReference(requireActivity()),
            store = requireComponents.core.store,
            navControllerRef = WeakReference(findNavController()),
            settings = requireComponents.settings,
            addTabUseCase = requireComponents.useCases.tabsUseCases.addTab,
            selectTabUseCase = requireComponents.useCases.tabsUseCases.selectTab,
            fenixBrowserUseCases = requireComponents.useCases.fenixBrowserUseCases,
            topSitesUseCases = requireComponents.useCases.topSitesUseCase,
            mozAdsUseCases = requireComponents.useCases.mozAdsUseCases,
            viewLifecycleScope = viewLifecycleOwner.lifecycleScope,
            source = TopSitesSource.SHORTCUTS_LIBRARY,
        )

        interactor = DefaultTopSiteInteractor(
            controller = controller,
        )

        topSitesBinding.set(
            feature = TopSitesBinding(
                browserStore = requireComponents.core.store,
                presenter = DefaultTopSitesPresenter(
                    view = DefaultTopSitesView(
                        appStore = requireComponents.appStore,
                        settings = requireComponents.settings,
                    ),
                    storage = requireComponents.core.topSitesStorage,
                    config = getTopSitesConfig(
                        settings = requireComponents.settings,
                        store = requireComponents.core.store,
                    ),
                ),
            ),
            owner = viewLifecycleOwner,
            view = view,
        )
    }

    override fun onCreateView(
        inflater: LayoutInflater,
        container: ViewGroup?,
        savedInstanceState: Bundle?,
    ): View = content {
        FirefoxTheme {
            ShortcutsScreen(
                store = shortcutsStore,
                interactor = interactor,
                onNavigationIconClick = {
                    this@ShortcutsFragment.findNavController().popBackStack()
                },
            )
        }
    }
}
